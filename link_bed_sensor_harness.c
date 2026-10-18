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

// Host bed-sensor harness: averaged samples in, occupancy + result out.
// The runner extracts the production state machine into bed_state.inc.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

#include "bed_state.inc"

#define THRESHOLD 2900
#define ABOVE     3600
#define BELOW     1800

static void check_result(void) {
    cJSON *r = bed_result(true, ABOVE, THRESHOLD, 90);
    assert(r != NULL);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(r, "ok")));
    cJSON *p = cJSON_GetObjectItem(r, "payload");
    assert(p != NULL);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(p, "occupied")));
    assert(cJSON_GetObjectItem(p, "raw")->valueint == ABOVE);
    assert(cJSON_GetObjectItem(p, "threshold")->valueint == THRESHOLD);
    assert(cJSON_GetObjectItem(p, "stable_s")->valueint == 90);
    char *text = cJSON_PrintUnformatted(r);
    assert(text != NULL);
    printf("result: %s\n", text);
    free(text);
    cJSON_Delete(r);
}

int main(void) {
    // In bed at once on the first above-threshold sample.
    bed_state_t s = {0};
    assert(bed_update(&s, 1000, BELOW, THRESHOLD) == false);
    assert(s.occupied == false);
    assert(bed_update(&s, 2000, ABOVE, THRESHOLD) == true);
    assert(s.occupied == true);
    assert(s.changed_at_ms == 2000);
    assert(bed_update(&s, 3000, ABOVE, THRESHOLD) == false);

    // A dip below holds for 60 s, then flips.
    assert(bed_update(&s, 4000, BELOW, THRESHOLD) == false);
    assert(s.occupied == true);
    assert(bed_update(&s, 4000 + 59000, BELOW, THRESHOLD) == false);
    assert(s.occupied == true);
    assert(bed_update(&s, 4000 + 60000, BELOW, THRESHOLD) == true);
    assert(s.occupied == false);

    // A spike above restarts the dwell: 59 s below, one above, then the
    // next 60 s below starts over instead of flipping early.
    bed_state_t t = {0};
    assert(bed_update(&t, 0, ABOVE, THRESHOLD) == true);
    assert(bed_update(&t, 1000, BELOW, THRESHOLD) == false);
    assert(bed_update(&t, 60000, BELOW, THRESHOLD) == false);
    assert(t.occupied == true);
    assert(bed_update(&t, 61000, ABOVE, THRESHOLD) == false);
    assert(t.occupied == true);
    assert(bed_update(&t, 62000, BELOW, THRESHOLD) == false);
    assert(bed_update(&t, 62000 + 59000, BELOW, THRESHOLD) == false);
    assert(t.occupied == true);
    assert(bed_update(&t, 62000 + 60000, BELOW, THRESHOLD) == true);
    assert(t.occupied == false);

    // On the line counts as below: strictly above the threshold is in bed.
    bed_state_t e = {0};
    assert(bed_update(&e, 0, THRESHOLD, THRESHOLD) == false);
    assert(e.occupied == false);

    // Millisecond wrap doesn't stick the dwell.
    bed_state_t w = {0};
    assert(bed_update(&w, 0, ABOVE, THRESHOLD) == true);
    uint32_t dip = UINT32_MAX - 1000u;
    assert(bed_update(&w, dip, BELOW, THRESHOLD) == false);
    assert(bed_update(&w, dip + 61000u, BELOW, THRESHOLD) == true);
    assert(w.occupied == false);

    check_result();
    printf("bed harness ok\n");
    return 0;
}
