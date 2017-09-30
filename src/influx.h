/*
 * eagle-owl InfluxDB consumer.
 *
 * Copyright (C) 2017 JP Meijers <eagle-owl@jpmeijers.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __INFLUX_H__
#define __INFLUX_H__
#include "db.h"

// struct record_data {
//   int addr;
//   int year;
//   int month;
//   int day;
//   int hour;
//   int min;
//   double cost;
//   double amps;
//   double watts;
//   double ah; // watt hour and ampere hour are the units used inside the db
//   double wh;
//   bool isLiveData; // Flag used to know is this record is the live conumption 
//                    // or the mean consumption (for the DB)
// };

void error(const char *msg);
int influxdb_insert_hist(struct record_data *rec);

#endif // __INFLUX_H__