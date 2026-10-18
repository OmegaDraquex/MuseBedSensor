/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Bed-occupancy sensor: an FSR force resistor under the mattress, wired as
// 3V3 -> FSR -> ADC pin -> pulldown -> GND, so the reading rises with
// pressure. A background task samples it once a second; the bed counts as
// occupied above the threshold, and needs 60 s continuously below it before
// it counts as out, so roll-overs don't flap the state.

#pragma once

#include <stdbool.h>

#include "cJSON.h"

// Start the sampler task. The threshold is the NVS override when one was set
// over the console, else CONFIG_HOMEHUB_BED_THRESHOLD.
void bed_sensor_init(void);

// bed.occupancy: occupied state, latest raw reading, effective threshold and
// how long the state has held, in seconds. An error until the first sample.
cJSON *bed_sensor_command(void);

// Console lines (without the '>'): "bed_threshold=<raw>" stores an in-situ
// calibration (500..4000, no restart needed), "bed_status" prints the live
// state. Returns false for other lines.
bool bed_sensor_console_command(const char *line);
