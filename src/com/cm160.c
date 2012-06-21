#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <usb.h>
#include <pthread.h>

#include "cm160.h"
#include "usb_utils.h"
#include "db.h"

#ifndef min
  #define min(a,b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef max
  #define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

static char ID_MSG[11]   = { 0xA9, 0x49, 0x44, 0x54, 0x43, 0x4D, 0x56, 0x30, 0x30, 0x31, 0x01 };
static char WAIT_MSG[11] = { 0xA9, 0x49, 0x44, 0x54, 0x57, 0x41, 0x49, 0x54, 0x50, 0x43, 0x52 };

#define FRAME_ID_LIVE 0x51 
#define FRAME_ID_DB   0x59 // value used to store in the DB (ch1_kw_avg)

#define HISTORY_SIZE 65536 // 30 * 24 * 60 = 43200 theoric history size

//struct usb_device *g_devices[MAX_DEVICES];
struct cm160_device g_devices[MAX_DEVICES];

/* prototype for thread routine */
void insert_db_history(void *num_elems)
{
  int i;
  printf("insert %d elems\n", num_elems);
  for(i=0; i<num_elems; i++)
  {
    unsigned char *frame = insert_db_history[i];
    int volts = 230;
    int year = frame[1]+2000;
    int month = frame[2];
    int day = frame[3];
    int hour = frame[4];
    int minutes = frame[5];
    //float cost = (frame[6]+(frame[7]<<8))/100.0;
    float amps = (frame[8]+(frame[9]<<8))*0.07;
    float watts = amps * volts;
    db_insert_hist(year, month, day, hour, minutes, watts, amps);
  }
}

static int process_frame(int dev_id, unsigned char *frame)
{
  static bool receive_history = true;
  static int id = 0;
  static char history[HISTORY_SIZE][11];
  int i;
  unsigned char data[1];
  unsigned int checksum = 0;
  usb_dev_handle *hdev = g_devices[dev_id].hdev;
  int epout = g_devices[dev_id].epout;

  if(strncmp((char *)frame, ID_MSG, 11) == 0)
  {
//    printf("received ID MSG\n");
    data[0]=0x5A;
    usb_bulk_write(hdev, epout, (const char *)&data, sizeof(data), 1000);
  }
  else if(strncmp((char *)frame, WAIT_MSG, 11) == 0)
  {
//    printf("received WAIT MSG\n");
    data[0]=0xA5;
    usb_bulk_write(hdev, epout, (const char *)&data, sizeof(data), 1000);
  }
  else
  {
    if(frame[0] != FRAME_ID_LIVE && frame[0] != FRAME_ID_DB)
    {
      printf("data error: invalid ID 0x%x\n", frame[0]);
      for(i=0; i<11; i++)
        printf("0x%02x - ", frame[i]);
      printf("\n");
      return -1;
    }

    for(i=0; i<10; i++)
      checksum += frame[i];
    checksum &= 0xff;
    if(checksum != frame[10])
    {
      printf("data error: invalid checksum: expected 0x%x, got 0x%x\n", frame[10], checksum);
      return -1;
    }

    int volts = 230;
    int year = frame[1]+2000;
    int month = frame[2];
    int day = frame[3];
    int hour = frame[4];
    int minutes = frame[5];
    float cost = (frame[6]+(frame[7]<<8))/100.0;
    float amps = (frame[8]+(frame[9]<<8))*0.07;
    float watts = amps * volts;

    if(frame[0]==FRAME_ID_DB)
    {
      if(receive_history && id < HISTORY_SIZE)
      {
        if(id == 0)
          printf("downloading history...\n");
        else if(id%10 == 0)
        {
          printf("\r %.1f%%", max(100, 100*((double)id/(30*24*60))));
          fflush(stdout);
        }
        memcpy(history[id++], frame, 11);
      }
      else
        db_insert_hist(year, month, day, hour, minutes, watts, amps);
    }
    else
    {
      if(receive_history)
      {
        printf("\rdownloading history... 100%%\n");
        fflush(stdout);
        receive_history = false;
        // insert the history into the db
        pthread_t thread;
        pthread_create(&thread, NULL, (void *)&insert_db_history, (void *)id);
      }
      printf("%02d/%02d/%04d %02d:%02d : %f kW %s\n", day, month, year, hour, minutes, watts,
        (frame[0]==FRAME_ID_LIVE)?" < LIVE":" < DB");
    }
  }
  return 0;
}

static int io_thread(int dev_id)
{
  int ret;
  usb_dev_handle *hdev = g_devices[dev_id].hdev;
  int epin = g_devices[dev_id].epin;
  unsigned char buffer[512];
  static unsigned char word[11];
  static int remaining = 11;

  memset(buffer, 0, sizeof(buffer));
  memset(word, 0, sizeof(word));

  while(1)
  {
    memset(buffer, 0, sizeof(buffer));
    ret = usb_bulk_read(hdev, epin, (char*)buffer, sizeof(buffer), 10000);
    if(ret < 0)
    {
      printf("bulk_read returned %d (%s)\n", ret, usb_strerror());
      return -1;
    }
//    printf("read %d bytes: \n", ret);
    unsigned char *bufptr = (unsigned char *)buffer;
/*
    if(remaining<11)
    {
      int rest = min(remaining, ret);
      memcpy((word+(11-remaining)), bufptr, rest);
      remaining -= rest;
      if(remaining == 0)
      {
        process_frame(dev_id, word);
        remaining = 11;
      }
      ret -= rest;
    }*/

    {
      int nb_words = ret/11;
      int rest = ret%11;
      while(nb_words--)
      {
        memcpy(word, bufptr, 11);
        bufptr+=11;
        process_frame(dev_id, word);
      };
/*  // usefull? seem that full word is retransmitted if not fully transmitted the first time.
      if(rest)
      {
        printf("rest %d chars\n", rest);
        memcpy(word, bufptr, rest);
        remaining -= rest;
      }*/
    }
  }
  return 0;
}

static int handle_device(int dev_id)
{
  int r, i;
  struct usb_device *dev = g_devices[dev_id].usb_dev;
  usb_dev_handle *hdev = g_devices[dev_id].hdev;

  usb_detach_kernel_driver_np(hdev, 0);

  if( 0 != (r = usb_set_configuration(hdev, dev->config[0].bConfigurationValue)) )
  {
    printf("usb_set_configuration returns %d (%s)\n", r, usb_strerror());
    return -1;
  }


  if((r = usb_claim_interface(hdev, 0)) < 0)
  {
    printf("Interface cannot be claimed: %d\n", r);
    return r;
  }

//  print_dev_infos(dev, hdev);

  int nep = dev->config->interface->altsetting->bNumEndpoints;
  for(i=0; i<nep; i++)
  {
    int ep = dev->config->interface->altsetting->endpoint[i].bEndpointAddress;
    if(ep&(1<<7))
      g_devices[dev_id].epin = ep;
    else
      g_devices[dev_id].epout = ep;
  }

  io_thread(dev_id);
 
  usb_release_interface(hdev, 0);
  return 0;
}

int main(int argc, char *argv[])
{
  int i, reset=0;
  int dev_cnt = scan_usb();
  printf("Found %d compatible device%s\n", dev_cnt, dev_cnt>1?"s":"");

  if(argc>1 && (strcmp(argv[1], "-r")==0) )
  {
    printf("reset devices!\n");
    reset = 1;
  }

  for(i = 0; i < dev_cnt; i++)
  {
    // do stuff with the detected device...
    if(!(g_devices[i].hdev = usb_open(g_devices[i].usb_dev)))
    {
      printf("failed to open device\n");
      break;
    }
    if(reset)
    {
      int ret = usb_reset(g_devices[i].hdev);
      printf("usb_reset returned %d\n", ret);
    }
    else
      handle_device(i);

    usb_close(g_devices[i].hdev);
  } 
  return 0;

}
