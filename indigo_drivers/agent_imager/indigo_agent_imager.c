// Copyright (c) 2018 CloudMakers, s. r. o.
// All rights reserved.
//
// You can use this software under the terms of 'INDIGO Astronomy
// open-source license' (see LICENSE.md).
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS 'AS IS' AND ANY EXPRESS
// OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
// GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// version history
// 2.0 by Peter Polakovic <peter.polakovic@cloudmakers.eu>

/** INDIGO Imager agent
 \file indigo_agent_imager.c
 */

#define DRIVER_VERSION 0x002C
#define DRIVER_NAME	"indigo_agent_imager"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <assert.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <indigo/indigo_driver_xml.h>
#include <indigo/indigo_filter.h>
#include <indigo/indigo_ccd_driver.h>
#include <indigo/indigo_io.h>
#include <indigo/indigo_raw_utils.h>
#include <indigo/indigo_align.h>
#include <indigo/indigo_polynomial_fit.h>

#include "indigo_agent_imager.h"

#define DEVICE_PRIVATE_DATA										((agent_private_data *)device->private_data)
#define CLIENT_PRIVATE_DATA										((agent_private_data *)FILTER_CLIENT_CONTEXT->device->private_data)

#define AGENT_IMAGER_BATCH_PROPERTY						(DEVICE_PRIVATE_DATA->agent_imager_batch_property)
#define AGENT_IMAGER_BATCH_COUNT_ITEM    			(AGENT_IMAGER_BATCH_PROPERTY->items+0)
#define AGENT_IMAGER_BATCH_EXPOSURE_ITEM  		(AGENT_IMAGER_BATCH_PROPERTY->items+1)
#define AGENT_IMAGER_BATCH_DELAY_ITEM     		(AGENT_IMAGER_BATCH_PROPERTY->items+2)
#define AGENT_IMAGER_BATCH_FRAMES_TO_SKIP_BEFORE_DITHER_ITEM	(AGENT_IMAGER_BATCH_PROPERTY->items+3)
#define AGENT_IMAGER_BATCH_PAUSE_AFTER_TRANSIT_ITEM     	(AGENT_IMAGER_BATCH_PROPERTY->items+4)

#define AGENT_IMAGER_FOCUS_PROPERTY						(DEVICE_PRIVATE_DATA->agent_imager_focus_property)
#define AGENT_IMAGER_FOCUS_INITIAL_ITEM    		(AGENT_IMAGER_FOCUS_PROPERTY->items+0)
#define AGENT_IMAGER_FOCUS_FINAL_ITEM  				(AGENT_IMAGER_FOCUS_PROPERTY->items+1)
#define AGENT_IMAGER_FOCUS_UCURVE_SAMPLES_ITEM  				(AGENT_IMAGER_FOCUS_PROPERTY->items+2)
#define AGENT_IMAGER_FOCUS_BACKLASH_ITEM     	(AGENT_IMAGER_FOCUS_PROPERTY->items+3)
#define AGENT_IMAGER_FOCUS_BACKLASH_IN_ITEM   (AGENT_IMAGER_FOCUS_PROPERTY->items+4)
#define AGENT_IMAGER_FOCUS_BACKLASH_OUT_ITEM  (AGENT_IMAGER_FOCUS_PROPERTY->items+5)
#define AGENT_IMAGER_FOCUS_BACKLASH_OVERSHOOT_ITEM  (AGENT_IMAGER_FOCUS_PROPERTY->items+6)
#define AGENT_IMAGER_FOCUS_STACK_ITEM					(AGENT_IMAGER_FOCUS_PROPERTY->items+7)
#define AGENT_IMAGER_FOCUS_REPEAT_ITEM				(AGENT_IMAGER_FOCUS_PROPERTY->items+8)
#define AGENT_IMAGER_FOCUS_DELAY_ITEM					(AGENT_IMAGER_FOCUS_PROPERTY->items+9)

#define AGENT_IMAGER_FOCUS_FAILURE_PROPERTY		(DEVICE_PRIVATE_DATA->agent_imager_focus_failure_property)
#define AGENT_IMAGER_FOCUS_FAILURE_STOP_ITEM  (AGENT_IMAGER_FOCUS_FAILURE_PROPERTY->items+0)
#define AGENT_IMAGER_FOCUS_FAILURE_RESTORE_ITEM  (AGENT_IMAGER_FOCUS_FAILURE_PROPERTY->items+1)

#define AGENT_IMAGER_FOCUS_ESTIMATOR_PROPERTY		(DEVICE_PRIVATE_DATA->agent_imager_focus_estimator_property)
#define AGENT_IMAGER_FOCUS_ESTIMATOR_UCURVE_ITEM  (AGENT_IMAGER_FOCUS_ESTIMATOR_PROPERTY->items+0)
#define AGENT_IMAGER_FOCUS_ESTIMATOR_HFD_PEAK_ITEM  (AGENT_IMAGER_FOCUS_ESTIMATOR_PROPERTY->items+1)
#define AGENT_IMAGER_FOCUS_ESTIMATOR_RMS_CONTRAST_ITEM  (AGENT_IMAGER_FOCUS_ESTIMATOR_PROPERTY->items+2)

#define AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY		(DEVICE_PRIVATE_DATA->agent_imager_download_file_property)
#define AGENT_IMAGER_DOWNLOAD_FILE_ITEM    		(AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY->items+0)

#define AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY	(DEVICE_PRIVATE_DATA->agent_imager_download_files_property)
#define AGENT_IMAGER_DOWNLOAD_FILES_REFRESH_ITEM    (AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY->items+0)
#define DOWNLOAD_MAX_COUNT										(INDIGO_MAX_ITEMS - 1)

#define AGENT_IMAGER_DOWNLOAD_IMAGE_PROPERTY	(DEVICE_PRIVATE_DATA->agent_imager_download_image_property)
#define AGENT_IMAGER_DOWNLOAD_IMAGE_ITEM    	(AGENT_IMAGER_DOWNLOAD_IMAGE_PROPERTY->items+0)

#define AGENT_IMAGER_DELETE_FILE_PROPERTY			(DEVICE_PRIVATE_DATA->agent_imager_delete_file_property)
#define AGENT_IMAGER_DELETE_FILE_ITEM    			(AGENT_IMAGER_DELETE_FILE_PROPERTY->items+0)

#define AGENT_START_PROCESS_PROPERTY					(DEVICE_PRIVATE_DATA->agent_start_process_property)
#define AGENT_IMAGER_START_PREVIEW_ITEM  			(AGENT_START_PROCESS_PROPERTY->items+0)
#define AGENT_IMAGER_START_EXPOSURE_ITEM  		(AGENT_START_PROCESS_PROPERTY->items+1)
#define AGENT_IMAGER_START_STREAMING_ITEM 		(AGENT_START_PROCESS_PROPERTY->items+2)
#define AGENT_IMAGER_START_FOCUSING_ITEM 			(AGENT_START_PROCESS_PROPERTY->items+3)
#define AGENT_IMAGER_START_SEQUENCE_ITEM 			(AGENT_START_PROCESS_PROPERTY->items+4)

#define AGENT_PAUSE_PROCESS_PROPERTY					(DEVICE_PRIVATE_DATA->agent_pause_process_property)
#define AGENT_PAUSE_PROCESS_ITEM      				(AGENT_PAUSE_PROCESS_PROPERTY->items+0)
#define AGENT_PAUSE_PROCESS_WAIT_ITEM      		(AGENT_PAUSE_PROCESS_PROPERTY->items+1)
#define AGENT_PAUSE_PROCESS_AFTER_TRANSIT_ITEM      	(AGENT_PAUSE_PROCESS_PROPERTY->items+2)

#define AGENT_ABORT_PROCESS_PROPERTY					(DEVICE_PRIVATE_DATA->agent_abort_process_property)
#define AGENT_ABORT_PROCESS_ITEM      				(AGENT_ABORT_PROCESS_PROPERTY->items+0)

#define AGENT_PROCESS_FEATURES_PROPERTY				(DEVICE_PRIVATE_DATA->agent_process_features_property)
#define AGENT_IMAGER_ENABLE_DITHERING_FEATURE_ITEM	(AGENT_PROCESS_FEATURES_PROPERTY->items+0)
#define AGENT_IMAGER_DITHER_AFTER_BATCH_FEATURE_ITEM	(AGENT_PROCESS_FEATURES_PROPERTY->items+1)
#define AGENT_IMAGER_PAUSE_AFTER_TRANSIT_FEATURE_ITEM		(AGENT_PROCESS_FEATURES_PROPERTY->items+2)

#define AGENT_WHEEL_FILTER_PROPERTY						(DEVICE_PRIVATE_DATA->agent_wheel_filter_property)
#define FILTER_SLOT_COUNT											24

#define AGENT_IMAGER_STATS_PROPERTY						(DEVICE_PRIVATE_DATA->agent_stats_property)
#define AGENT_IMAGER_STATS_EXPOSURE_ITEM      (AGENT_IMAGER_STATS_PROPERTY->items+0)
#define AGENT_IMAGER_STATS_DELAY_ITEM      		(AGENT_IMAGER_STATS_PROPERTY->items+1)
#define AGENT_IMAGER_STATS_FRAME_ITEM      		(AGENT_IMAGER_STATS_PROPERTY->items+2)
#define AGENT_IMAGER_STATS_FRAMES_ITEM      	(AGENT_IMAGER_STATS_PROPERTY->items+3)
#define AGENT_IMAGER_STATS_BATCH_INDEX_ITEM   (AGENT_IMAGER_STATS_PROPERTY->items+4)
#define AGENT_IMAGER_STATS_BATCH_ITEM      		(AGENT_IMAGER_STATS_PROPERTY->items+5)
#define AGENT_IMAGER_STATS_BATCHES_ITEM      	(AGENT_IMAGER_STATS_PROPERTY->items+6)
#define AGENT_IMAGER_STATS_PHASE_ITEM  				(AGENT_IMAGER_STATS_PROPERTY->items+7)
#define AGENT_IMAGER_STATS_DRIFT_X_ITEM      	(AGENT_IMAGER_STATS_PROPERTY->items+8)
#define AGENT_IMAGER_STATS_DRIFT_Y_ITEM      	(AGENT_IMAGER_STATS_PROPERTY->items+9)
#define AGENT_IMAGER_STATS_FWHM_ITEM      		(AGENT_IMAGER_STATS_PROPERTY->items+10)
#define AGENT_IMAGER_STATS_HFD_ITEM      			(AGENT_IMAGER_STATS_PROPERTY->items+11)
#define AGENT_IMAGER_STATS_PEAK_ITEM      		(AGENT_IMAGER_STATS_PROPERTY->items+12)
#define AGENT_IMAGER_STATS_DITHERING_ITEM     (AGENT_IMAGER_STATS_PROPERTY->items+13)
#define AGENT_IMAGER_STATS_FOCUS_OFFSET_ITEM     		(AGENT_IMAGER_STATS_PROPERTY->items+14)
#define AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM     		(AGENT_IMAGER_STATS_PROPERTY->items+15)
#define AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM			(AGENT_IMAGER_STATS_PROPERTY->items+16)
#define AGENT_IMAGER_STATS_FRAMES_TO_DITHERING_ITEM (AGENT_IMAGER_STATS_PROPERTY->items+17)

#define MAX_STAR_COUNT												50
#define AGENT_IMAGER_STARS_PROPERTY						(DEVICE_PRIVATE_DATA->agent_stars_property)
#define AGENT_IMAGER_STARS_REFRESH_ITEM  			(AGENT_IMAGER_STARS_PROPERTY->items+0)

#define AGENT_IMAGER_SELECTION_PROPERTY				(DEVICE_PRIVATE_DATA->agent_selection_property)
#define AGENT_IMAGER_SELECTION_RADIUS_ITEM  	(AGENT_IMAGER_SELECTION_PROPERTY->items+0)
#define AGENT_IMAGER_SELECTION_SUBFRAME_ITEM	(AGENT_IMAGER_SELECTION_PROPERTY->items+1)
#define AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM (AGENT_IMAGER_SELECTION_PROPERTY->items+2)
#define AGENT_IMAGER_SELECTION_X_ITEM  				(AGENT_IMAGER_SELECTION_PROPERTY->items+3)
#define AGENT_IMAGER_SELECTION_Y_ITEM  				(AGENT_IMAGER_SELECTION_PROPERTY->items+4)

#define AGENT_IMAGER_SEQUENCE_PROPERTY				(DEVICE_PRIVATE_DATA->agent_sequence)
#define AGENT_IMAGER_SEQUENCE_ITEM						(AGENT_IMAGER_SEQUENCE_PROPERTY->items+0)

#define AGENT_IMAGER_SEQUENCE_SIZE_PROPERTY				(DEVICE_PRIVATE_DATA->agent_sequence_size)
#define AGENT_IMAGER_SEQUENCE_SIZE_ITEM					(AGENT_IMAGER_SEQUENCE_SIZE_PROPERTY->items+0)

#define AGENT_IMAGER_BREAKPOINT_PROPERTY						(DEVICE_PRIVATE_DATA->agent_breakpoint_property)
#define AGENT_IMAGER_BREAKPOINT_PRE_BATCH_ITEM			(AGENT_IMAGER_BREAKPOINT_PROPERTY->items+0)
#define AGENT_IMAGER_BREAKPOINT_PRE_CAPTURE_ITEM		(AGENT_IMAGER_BREAKPOINT_PROPERTY->items+1)
#define AGENT_IMAGER_BREAKPOINT_POST_CAPTURE_ITEM		(AGENT_IMAGER_BREAKPOINT_PROPERTY->items+2)
#define AGENT_IMAGER_BREAKPOINT_PRE_DELAY_ITEM			(AGENT_IMAGER_BREAKPOINT_PROPERTY->items+3)
#define AGENT_IMAGER_BREAKPOINT_POST_DELAY_ITEM			(AGENT_IMAGER_BREAKPOINT_PROPERTY->items+4)
#define AGENT_IMAGER_BREAKPOINT_POST_BATCH_ITEM			(AGENT_IMAGER_BREAKPOINT_PROPERTY->items+5)

#define AGENT_IMAGER_RESUME_CONDITION_PROPERTY			(DEVICE_PRIVATE_DATA->agent_resume_condition_property)
#define AGENT_IMAGER_RESUME_CONDITION_TRIGGER_ITEM	(AGENT_IMAGER_RESUME_CONDITION_PROPERTY->items+0)
#define AGENT_IMAGER_RESUME_CONDITION_BARRIER_ITEM	(AGENT_IMAGER_RESUME_CONDITION_PROPERTY->items+1)

#define AGENT_IMAGER_BARRIER_STATE_PROPERTY					(DEVICE_PRIVATE_DATA->agent_barrier_property)

#define SEQUENCE_SIZE					16
#define MAX_SEQUENCE_SIZE				128

#define BUSY_TIMEOUT 5
#define AF_MOVE_LIMIT_HFD 20
#define AF_MOVE_LIMIT_RMS 40
#define AF_MOVE_LIMIT_UCURVE 10

#define MAX_UCURVE_SAMPLES 24
#define UCURVE_ORDER 4

#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

typedef struct {
	indigo_property *agent_imager_batch_property;
	indigo_property *agent_imager_focus_property;
	indigo_property *agent_imager_focus_failure_property;
	indigo_property *agent_imager_focus_estimator_property;
	indigo_property *agent_imager_download_file_property;
	indigo_property *agent_imager_download_files_property;
	indigo_property *agent_imager_download_image_property;
	indigo_property *agent_imager_delete_file_property;
	indigo_property *agent_start_process_property;
	indigo_property *agent_pause_process_property;
	indigo_property *agent_abort_process_property;
	indigo_property *agent_process_features_property;
	indigo_property *agent_wheel_filter_property;
	indigo_property *agent_stars_property;
	indigo_property *agent_selection_property;
	indigo_property *agent_stats_property;
	indigo_property *agent_sequence_size;;
	indigo_property *agent_sequence;
	indigo_property *agent_sequence_state;
	indigo_property *agent_breakpoint_property;
	indigo_property *agent_resume_condition_property;
	indigo_property *agent_barrier_property;
	indigo_property *saved_frame;
	double saved_frame_left, saved_frame_top;
	char current_folder[INDIGO_VALUE_SIZE];
	void *image_buffer;
	size_t image_buffer_size;
	double focuser_position;
	double saved_backlash;
	int ucurve_samples_number;
	indigo_star_detection stars[MAX_STAR_COUNT];
	indigo_frame_digest reference;
	double drift_x, drift_y;
	int bin_x, bin_y;
	void *last_image;
	size_t last_image_size;
	pthread_mutex_t mutex;
	double focus_exposure;
	bool dithering_started, dithering_finished, guiding;
	bool allow_subframing;
	bool frame_saturated;
	bool find_stars;
	bool focuser_has_backlash;
	bool restore_initial_position;
	bool use_hfd_estimator;
	bool use_ucurve_estimator;
	bool use_rms_estimator;
	bool use_aux_1;
	bool barrier_resume;
	unsigned int dither_num;
	indigo_property_state related_solver_process_state;
	indigo_property_state related_guider_process_state;
	double solver_goto_ra;
	double solver_goto_dec;
	double ra, dec, latitude, longitude, time_to_transit;
} agent_private_data;

// -------------------------------------------------------------------------------- INDIGO agent common code

static indigo_property_state capture_raw_frame(indigo_device *device, uint8_t **saturation_mask);
static indigo_property_state _capture_raw_frame(indigo_device *device, uint8_t **saturation_mask, bool is_restore_frame);

static void save_config(indigo_device *device) {
	if (pthread_mutex_trylock(&DEVICE_CONTEXT->config_mutex) == 0) {
		pthread_mutex_unlock(&DEVICE_CONTEXT->config_mutex);
		pthread_mutex_lock(&DEVICE_PRIVATE_DATA->mutex);
		indigo_save_property(device, NULL, AGENT_IMAGER_BATCH_PROPERTY);
		indigo_save_property(device, NULL, AGENT_IMAGER_FOCUS_PROPERTY);
		indigo_save_property(device, NULL, AGENT_IMAGER_FOCUS_FAILURE_PROPERTY);
		indigo_save_property(device, NULL, AGENT_IMAGER_FOCUS_ESTIMATOR_PROPERTY);
		indigo_save_property(device, NULL, AGENT_IMAGER_SEQUENCE_SIZE_PROPERTY);
		indigo_save_property(device, NULL, AGENT_IMAGER_SEQUENCE_PROPERTY);
		indigo_save_property(device, NULL, ADDITIONAL_INSTANCES_PROPERTY);
		indigo_save_property(device, NULL, AGENT_PROCESS_FEATURES_PROPERTY);
		char *selection_property_items[] = { AGENT_IMAGER_SELECTION_RADIUS_ITEM_NAME, AGENT_IMAGER_SELECTION_SUBFRAME_ITEM_NAME, AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM_NAME };
		indigo_save_property_items(device, NULL, AGENT_IMAGER_SELECTION_PROPERTY, 3, (const char **)selection_property_items);
		if (DEVICE_CONTEXT->property_save_file_handle) {
			CONFIG_PROPERTY->state = INDIGO_OK_STATE;
			close(DEVICE_CONTEXT->property_save_file_handle);
			DEVICE_CONTEXT->property_save_file_handle = 0;
		} else {
			CONFIG_PROPERTY->state = INDIGO_ALERT_STATE;
		}
		CONFIG_SAVE_ITEM->sw.value = false;
		indigo_update_property(device, CONFIG_PROPERTY, NULL);
		pthread_mutex_unlock(&DEVICE_PRIVATE_DATA->mutex);
	}
}

static int save_switch_state(indigo_device *device, int index, char *name, char *new_state) {
	indigo_property *device_property;
	if (indigo_filter_cached_property(device, index, name, &device_property, NULL)) {
		for (int i = 0; i < device_property->count; i++) {
			if (device_property->items[i].sw.value) {
				if (new_state) {
					indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, new_state, true);
				}
				return i;
			}
		}
	}
	return -1;
}

static void restore_switch_state(indigo_device *device, int index, char *name, int state) {
	if (state >= 0) {
		indigo_property *device_property;
		if (indigo_filter_cached_property(device, index, name, &device_property, NULL)) {
			indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, device_property->items[state].name, true);
		}
	}
}

static void set_headers(indigo_device *device) {
	if (!AGENT_WHEEL_FILTER_PROPERTY->hidden) {
		for (int i = 0; i < AGENT_WHEEL_FILTER_PROPERTY->count; i++) {
			indigo_item *item = AGENT_WHEEL_FILTER_PROPERTY->items + i;
			if (item->sw.value) {
				indigo_set_fits_header(FILTER_DEVICE_CONTEXT->client, FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_CCD_INDEX], "FILTER", "'%s'", item->label);
				break;
			}
		}
	} else {
		indigo_remove_fits_header(FILTER_DEVICE_CONTEXT->client, FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_CCD_INDEX], "FILTER");
	}
	if (*FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_FOCUSER_INDEX]) {
		if (DEVICE_PRIVATE_DATA->focuser_position - rint(DEVICE_PRIVATE_DATA->focuser_position) < 0.00001) {
			indigo_set_fits_header(FILTER_DEVICE_CONTEXT->client, FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_CCD_INDEX], "FOCUSPOS", "%d", (int)DEVICE_PRIVATE_DATA->focuser_position);
		} else {
			indigo_set_fits_header(FILTER_DEVICE_CONTEXT->client, FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_CCD_INDEX], "FOCUSPOS", "%.5f", DEVICE_PRIVATE_DATA->focuser_position);
		}
	} else {
		indigo_remove_fits_header(FILTER_DEVICE_CONTEXT->client, FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_CCD_INDEX], "FOCUSPOS");
	}
}

static void park_mount(indigo_device *device) {
	char *related_agent_name = indigo_filter_first_related_agent(device, "Mount Agent");
	if (related_agent_name) {
		indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, related_agent_name, MOUNT_PARK_PROPERTY_NAME, MOUNT_PARK_PARKED_ITEM_NAME, true);
	}
}

static void unpark_mount(indigo_device *device) {
	char *related_agent_name = indigo_filter_first_related_agent(device, "Mount Agent");
	if (related_agent_name) {
		indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, related_agent_name, MOUNT_PARK_PROPERTY_NAME, MOUNT_PARK_UNPARKED_ITEM_NAME, true);
	}
}

static void solver_precise_goto(indigo_device *device) {
	char *related_agent_name = indigo_filter_first_related_agent_2(device, "Astrometry Agent", "ASTAP Agent");
	if (related_agent_name) {
		char *names[] = { AGENT_PLATESOLVER_GOTO_SETTINGS_RA_ITEM_NAME, AGENT_PLATESOLVER_GOTO_SETTINGS_DEC_ITEM_NAME };
		double values[] = { DEVICE_PRIVATE_DATA->solver_goto_ra, DEVICE_PRIVATE_DATA->solver_goto_dec };
		indigo_change_number_property(FILTER_DEVICE_CONTEXT->client, related_agent_name, AGENT_PLATESOLVER_GOTO_SETTINGS_PROPERTY_NAME, 2, (const char **)names, values);
		indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, related_agent_name, AGENT_PLATESOLVER_SOLVE_IMAGES_PROPERTY_NAME, AGENT_PLATESOLVER_SOLVE_IMAGES_ENABLED_ITEM_NAME, true);
		indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, related_agent_name, AGENT_START_PROCESS_PROPERTY_NAME, AGENT_PLATESOLVER_START_PRECISE_GOTO_ITEM_NAME, true);
	}
}

static void disable_solver(indigo_device *device) {
	char *related_agent_name = indigo_filter_first_related_agent_2(device, "Astrometry Agent", "ASTAP Agent");
	if (related_agent_name) {
		indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, related_agent_name, AGENT_PLATESOLVER_SOLVE_IMAGES_PROPERTY_NAME, AGENT_PLATESOLVER_SOLVE_IMAGES_DISABLED_ITEM_NAME, true);
	}
}

static void abort_solver(indigo_device *device) {
	char *related_agent_name = indigo_filter_first_related_agent_2(device, "Astrometry Agent", "ASTAP Agent");
	if (related_agent_name) {
		indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, related_agent_name, AGENT_ABORT_PROCESS_PROPERTY_NAME, AGENT_ABORT_PROCESS_ITEM_NAME, true);
	}
}

static void allow_abort_by_mount_agent(indigo_device *device, bool state) {
	char *related_agent_name = indigo_filter_first_related_agent(device, "Mount Agent");
	if (related_agent_name) {
		indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, related_agent_name, AGENT_ABORT_RELATED_PROCESS_PROPERTY_NAME, AGENT_ABORT_IMAGER_ITEM_NAME, state);
	}
}

static void stop_guider(indigo_device *device) {
	char *related_agent_name = indigo_filter_first_related_agent(device, "Guider Agent");
	if (related_agent_name) {
		indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, related_agent_name, AGENT_ABORT_PROCESS_PROPERTY_NAME, AGENT_ABORT_PROCESS_ITEM_NAME, true);
	}
}

static void calibrate_guider(indigo_device *device, double exposure_time) {
	char *related_agent_name = indigo_filter_first_related_agent(device, "Guider Agent");
	if (related_agent_name) {
		indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, related_agent_name, AGENT_GUIDER_SETTINGS_PROPERTY_NAME, AGENT_GUIDER_SETTINGS_EXPOSURE_ITEM_NAME, exposure_time);
		indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, related_agent_name, AGENT_START_PROCESS_PROPERTY_NAME, AGENT_GUIDER_START_CALIBRATION_AND_GUIDING_ITEM_NAME, true);
	}
}

static void start_guider(indigo_device *device, double exposure_time) {
	char *related_agent_name = indigo_filter_first_related_agent(device, "Guider Agent");
	if (related_agent_name) {
		indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, related_agent_name, AGENT_GUIDER_SETTINGS_PROPERTY_NAME, AGENT_GUIDER_SETTINGS_EXPOSURE_ITEM_NAME, exposure_time);
		indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, related_agent_name, AGENT_START_PROCESS_PROPERTY_NAME, AGENT_GUIDER_START_GUIDING_ITEM_NAME, true);
	}
}

#define GRID	32

static void select_subframe(indigo_device *device) {
	int selection_x = AGENT_IMAGER_SELECTION_X_ITEM->number.value;
	int selection_y = AGENT_IMAGER_SELECTION_Y_ITEM->number.value;
	if (selection_x && selection_y && AGENT_IMAGER_SELECTION_SUBFRAME_ITEM->number.value && DEVICE_PRIVATE_DATA->saved_frame == NULL) {
		indigo_property *device_ccd_frame_property, *agent_ccd_frame_property;
		if (indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, CCD_FRAME_PROPERTY_NAME, &device_ccd_frame_property, &agent_ccd_frame_property) && agent_ccd_frame_property->perm == INDIGO_RW_PERM) {
			for (int i = 0; i < agent_ccd_frame_property->count; i++) {
				indigo_item *item = agent_ccd_frame_property->items + i;
				if (!strcmp(item->name, CCD_FRAME_LEFT_ITEM_NAME))
					selection_x += item->number.value / DEVICE_PRIVATE_DATA->bin_x;
				else if (!strcmp(item->name, CCD_FRAME_TOP_ITEM_NAME))
					selection_y += item->number.value / DEVICE_PRIVATE_DATA->bin_y;
			}
			int window_size = AGENT_IMAGER_SELECTION_SUBFRAME_ITEM->number.value * AGENT_IMAGER_SELECTION_RADIUS_ITEM->number.value;
			if (window_size < GRID)
				window_size = GRID;
			int frame_left = rint((selection_x - window_size) / (double)GRID) * GRID;
			int frame_top = rint((selection_y - window_size) / (double)GRID) * GRID;
			if (selection_x - frame_left < AGENT_IMAGER_SELECTION_RADIUS_ITEM->number.value)
				frame_left -= GRID;
			if (selection_y - frame_top < AGENT_IMAGER_SELECTION_RADIUS_ITEM->number.value)
				frame_top -= GRID;
			int frame_width = (2 * window_size / GRID + 1) * GRID;
			int frame_height = (2 * window_size / GRID + 1) * GRID;
			DEVICE_PRIVATE_DATA->saved_frame_left = frame_left;
			DEVICE_PRIVATE_DATA->saved_frame_top = frame_top;
			AGENT_IMAGER_SELECTION_X_ITEM->number.value = selection_x -= frame_left;
			AGENT_IMAGER_SELECTION_Y_ITEM->number.value = selection_y -= frame_top;
			indigo_update_property(device, AGENT_IMAGER_SELECTION_PROPERTY, NULL);
			if (frame_width - selection_x < AGENT_IMAGER_SELECTION_RADIUS_ITEM->number.value)
				frame_width += GRID;
			if (frame_height - selection_y < AGENT_IMAGER_SELECTION_RADIUS_ITEM->number.value)
				frame_height += GRID;
			int size = sizeof(indigo_property) + device_ccd_frame_property->count * sizeof(indigo_item);
			DEVICE_PRIVATE_DATA->saved_frame = indigo_safe_malloc_copy(size, agent_ccd_frame_property);
			strcpy(DEVICE_PRIVATE_DATA->saved_frame->device, device_ccd_frame_property->device);
			char *names[] = { CCD_FRAME_LEFT_ITEM_NAME, CCD_FRAME_TOP_ITEM_NAME, CCD_FRAME_WIDTH_ITEM_NAME, CCD_FRAME_HEIGHT_ITEM_NAME };
			double values[] = { frame_left * DEVICE_PRIVATE_DATA->bin_x, frame_top * DEVICE_PRIVATE_DATA->bin_y,  frame_width * DEVICE_PRIVATE_DATA->bin_x, frame_height * DEVICE_PRIVATE_DATA->bin_y };
			indigo_change_number_property(FILTER_DEVICE_CONTEXT->client, device_ccd_frame_property->device, CCD_FRAME_PROPERTY_NAME, 4, (const char **)names, values);
		}
	}
}

static void restore_subframe(indigo_device *device) {
	if (DEVICE_PRIVATE_DATA->saved_frame) {
		indigo_change_property(FILTER_DEVICE_CONTEXT->client, DEVICE_PRIVATE_DATA->saved_frame);
		indigo_release_property(DEVICE_PRIVATE_DATA->saved_frame);
		DEVICE_PRIVATE_DATA->saved_frame = NULL;
		AGENT_IMAGER_SELECTION_X_ITEM->number.value += DEVICE_PRIVATE_DATA->saved_frame_left;
		AGENT_IMAGER_SELECTION_X_ITEM->number.target = AGENT_IMAGER_SELECTION_X_ITEM->number.value;
		AGENT_IMAGER_SELECTION_Y_ITEM->number.value += DEVICE_PRIVATE_DATA->saved_frame_top;
		AGENT_IMAGER_SELECTION_Y_ITEM->number.target = AGENT_IMAGER_SELECTION_Y_ITEM->number.value;
		/* TRICKY: No idea why but this prevents ensures frame to be restored correctly */
		indigo_usleep(0.5 * ONE_SECOND_DELAY);
		/* TRICKY: capture_raw_frame() should be here in order to have the correct frame and correct selection
			 but selection property should not be updated. */
		_capture_raw_frame(device, NULL, true);
		indigo_update_property(device, AGENT_IMAGER_SELECTION_PROPERTY, NULL);
		DEVICE_PRIVATE_DATA->saved_frame_left = 0;
		DEVICE_PRIVATE_DATA->saved_frame_top = 0;
	}
}

static indigo_property_state _capture_raw_frame(indigo_device *device, uint8_t **saturation_mask, bool is_restore_frame) {
	indigo_property_state state = INDIGO_ALERT_STATE;
	indigo_property *device_exposure_property, *agent_exposure_property, *device_aux_1_exposure_property, *agent_aux_1_exposure_property, *device_format_property;
	DEVICE_PRIVATE_DATA->use_aux_1 = false;
	DEVICE_PRIVATE_DATA->frame_saturated = false;
	if (DEVICE_PRIVATE_DATA->last_image) {
		free (DEVICE_PRIVATE_DATA->last_image);
		DEVICE_PRIVATE_DATA->last_image = NULL;
		DEVICE_PRIVATE_DATA->last_image_size = 0;
	}
	if (indigo_filter_cached_property(device, INDIGO_FILTER_AUX_1_INDEX, CCD_EXPOSURE_PROPERTY_NAME, &device_aux_1_exposure_property, &agent_aux_1_exposure_property)) {
		DEVICE_PRIVATE_DATA->use_aux_1 = true;
	}
	if (!indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, CCD_EXPOSURE_PROPERTY_NAME, &device_exposure_property, &agent_exposure_property)) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "CCD_EXPOSURE not found");
		return INDIGO_ALERT_STATE;
	}
	if (!indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, CCD_IMAGE_FORMAT_PROPERTY_NAME, &device_format_property, NULL)) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "CCD_IMAGE_FORMAT not found");
		return INDIGO_ALERT_STATE;
	}
	indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, device_exposure_property->device, CCD_IMAGE_FORMAT_PROPERTY_NAME, CCD_IMAGE_FORMAT_RAW_ITEM_NAME, true);
	FILTER_DEVICE_CONTEXT->property_removed = false;
	for (int exposure_attempt = 0; exposure_attempt < 3; exposure_attempt++) {
		if (FILTER_DEVICE_CONTEXT->property_removed)
			return INDIGO_ALERT_STATE;
		while (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
			indigo_usleep(200000);
		if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
			return INDIGO_ALERT_STATE;
		if (DEVICE_PRIVATE_DATA->use_aux_1) {
			indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, device_exposure_property->device, CCD_EXPOSURE_PROPERTY_NAME, CCD_EXPOSURE_ITEM_NAME, 0);
			indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, device_aux_1_exposure_property->device, CCD_EXPOSURE_PROPERTY_NAME, CCD_EXPOSURE_ITEM_NAME, AGENT_IMAGER_BATCH_EXPOSURE_ITEM->number.target);
		} else {
			indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, device_exposure_property->device, CCD_EXPOSURE_PROPERTY_NAME, CCD_EXPOSURE_ITEM_NAME, AGENT_IMAGER_BATCH_EXPOSURE_ITEM->number.target);
		}
		for (int i = 0; i < BUSY_TIMEOUT * 1000 && !FILTER_DEVICE_CONTEXT->property_removed && (state = agent_exposure_property->state) != INDIGO_BUSY_STATE && AGENT_ABORT_PROCESS_PROPERTY->state != INDIGO_BUSY_STATE && AGENT_PAUSE_PROCESS_PROPERTY->state != INDIGO_BUSY_STATE; i++)
			indigo_usleep(1000);
		if (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			while (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
				indigo_usleep(200000);
			if (AGENT_PAUSE_PROCESS_ITEM->sw.value) {
				exposure_attempt--;
				continue;
			}
		}
		if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
			return INDIGO_ALERT_STATE;
		if (FILTER_DEVICE_CONTEXT->property_removed || state != INDIGO_BUSY_STATE) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "CCD_EXPOSURE didn't become busy in %d second(s)", BUSY_TIMEOUT);
			indigo_usleep(ONE_SECOND_DELAY);
			continue;
		}
		double reported_exposure_time = DEVICE_PRIVATE_DATA->use_aux_1 ?  agent_aux_1_exposure_property->items[0].number.value : agent_exposure_property->items[0].number.value;
		AGENT_IMAGER_STATS_EXPOSURE_ITEM->number.value = reported_exposure_time;
		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		while (!FILTER_DEVICE_CONTEXT->property_removed && (state = agent_exposure_property->state) == INDIGO_BUSY_STATE) {
			if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
				return INDIGO_ALERT_STATE;
			if (reported_exposure_time != agent_exposure_property->items[0].number.value) {
				AGENT_IMAGER_STATS_EXPOSURE_ITEM->number.value = reported_exposure_time = agent_exposure_property->items[0].number.value;
				indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
			}
			if (reported_exposure_time > 1) {
				indigo_usleep(200000);
			} else {
				indigo_usleep(10000);
			}
		}
		if (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			while (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
				indigo_usleep(200000);
			if (AGENT_PAUSE_PROCESS_ITEM->sw.value) {
				exposure_attempt--;
				continue;
			}
		}
		if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
			return INDIGO_ALERT_STATE;
		if (state != INDIGO_OK_STATE) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "CCD_EXPOSURE_PROPERTY didn't become OK");
			indigo_usleep(ONE_SECOND_DELAY);
			continue;
		}
		break;
	}
	if (FILTER_DEVICE_CONTEXT->property_removed || state != INDIGO_OK_STATE) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "Exposure failed");
		return INDIGO_ALERT_STATE;
	}

	indigo_raw_header *header = (indigo_raw_header *)(DEVICE_PRIVATE_DATA->last_image);
	if (header == NULL || (header->signature != INDIGO_RAW_MONO8 && header->signature != INDIGO_RAW_MONO16 && header->signature != INDIGO_RAW_RGB24 && header->signature != INDIGO_RAW_RGB48)) {
		indigo_send_message(device, "No RAW image received");
		return INDIGO_ALERT_STATE;
	}

	/* This is potentially bayered image, if so we need to equalize the channels */
	if (indigo_is_bayered_image(header, DEVICE_PRIVATE_DATA->last_image_size)) {
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Bayered image detected, equalizing channels");
		indigo_equalize_bayer_channels(header->signature, (void*)header + sizeof(indigo_raw_header), header->width, header->height);
	}

	/* if frame changes, contrast changes too, so do not change AGENT_IMAGER_STATS_RMS_CONTRAST item if this frame is to restore the full frame */
	if (saturation_mask && DEVICE_PRIVATE_DATA->use_rms_estimator && !is_restore_frame) {
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "focus_saturation_mask = 0x%p", *saturation_mask);
		AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value = indigo_contrast(header->signature, (void*)header + sizeof(indigo_raw_header), *saturation_mask, header->width, header->height, &DEVICE_PRIVATE_DATA->frame_saturated);
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "frame contrast = %f %s", AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value, DEVICE_PRIVATE_DATA->frame_saturated ? "(saturated)" : "");
		if (DEVICE_PRIVATE_DATA->frame_saturated) {
			if (
				header->signature == INDIGO_RAW_MONO8 ||
				header->signature == INDIGO_RAW_MONO16 ||
				header->signature == INDIGO_RAW_RGB24 ||
				header->signature == INDIGO_RAW_RGB48
			) {
				indigo_send_message(device, "Warning: Frame saturation detected, masking out saturated areas and resetting statistics");
				if (*saturation_mask == NULL) {
					indigo_init_saturation_mask(header->width, header->height, saturation_mask);
				}
				indigo_update_saturation_mask(header->signature, (void*)header + sizeof(indigo_raw_header), header->width, header->height, *saturation_mask);
				AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value = indigo_contrast(header->signature, (void*)header + sizeof(indigo_raw_header), *saturation_mask, header->width, header->height, NULL);
				AGENT_IMAGER_STATS_FRAME_ITEM->number.value = 0;
			} else {  // Colour image saturation masking is not supported yet.
				indigo_send_message(device, "Warning: Frame saturation detected, final focus may not be accurate");
				DEVICE_PRIVATE_DATA->frame_saturated = false;
			}
		}
	} else if (DEVICE_PRIVATE_DATA->use_hfd_estimator || DEVICE_PRIVATE_DATA->use_ucurve_estimator) {
		if ((AGENT_IMAGER_SELECTION_X_ITEM->number.value > 0 && AGENT_IMAGER_SELECTION_Y_ITEM->number.value > 0) || DEVICE_PRIVATE_DATA->allow_subframing || DEVICE_PRIVATE_DATA->find_stars) {
			if (DEVICE_PRIVATE_DATA->find_stars || (AGENT_IMAGER_SELECTION_X_ITEM->number.value == 0 && AGENT_IMAGER_SELECTION_Y_ITEM->number.value == 0 && AGENT_IMAGER_STARS_PROPERTY->count == 1)) {
				int star_count;
				indigo_delete_property(device, AGENT_IMAGER_STARS_PROPERTY, NULL);
				indigo_find_stars_precise(
					header->signature,
					(void*)header + sizeof(indigo_raw_header),
					AGENT_IMAGER_SELECTION_RADIUS_ITEM->number.value,
					header->width,
					header->height,
					MAX_STAR_COUNT,
					(indigo_star_detection *)&DEVICE_PRIVATE_DATA->stars,
					&star_count
				);
				AGENT_IMAGER_STARS_PROPERTY->count = star_count + 1;
				for (int i = 0; i < star_count; i++) {
					char name[8];
					char label[INDIGO_NAME_SIZE];
					snprintf(name, sizeof(name), "%d", i);
					snprintf(label, sizeof(label), "[%d, %d]", (int)DEVICE_PRIVATE_DATA->stars[i].x, (int)DEVICE_PRIVATE_DATA->stars[i].y);
					indigo_init_switch_item(AGENT_IMAGER_STARS_PROPERTY->items + i + 1, name, label, false);
				}
				AGENT_IMAGER_STARS_PROPERTY->state = INDIGO_OK_STATE;
				indigo_define_property(device, AGENT_IMAGER_STARS_PROPERTY, NULL);
				DEVICE_PRIVATE_DATA->find_stars = false;
				if (star_count == 0) {
					if (AGENT_IMAGER_START_PREVIEW_ITEM->sw.value) {
						return INDIGO_OK_STATE;
					} else {
						indigo_send_message(device, "No stars detected");
						return INDIGO_ALERT_STATE;
					}
				}
			}
			if (AGENT_IMAGER_SELECTION_X_ITEM->number.value == 0 && AGENT_IMAGER_SELECTION_Y_ITEM->number.value == 0 && AGENT_IMAGER_STARS_PROPERTY->count > 1) {
				int selection_index = 0;
				for (int i = 0; i < AGENT_IMAGER_STARS_PROPERTY->count && selection_index < AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM->number.value; i++) {
					indigo_star_detection *star = DEVICE_PRIVATE_DATA->stars + i;
					INDIGO_DRIVER_DEBUG(DRIVER_NAME, "star #%d -> oversturated = %d, NCD = %g, close_to_other = %d", i, star->oversaturated, star->nc_distance, star->close_to_other);
					if (star->oversaturated || star->nc_distance > 0.5 || star->close_to_other)
						continue;
					indigo_item *item_x = AGENT_IMAGER_SELECTION_X_ITEM + 2 * selection_index;
					indigo_item *item_y = AGENT_IMAGER_SELECTION_Y_ITEM + 2 * selection_index;
					item_x->number.target = item_x->number.value = star->x;
					item_y->number.target = item_y->number.value = star->y;
					selection_index++;
				}
				if (selection_index < AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM->number.value) {
					indigo_send_message(device, "Warning: Only %d suitable stars found (%d requested).", selection_index, (int)AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM->number.value);
				} else {
					for (int i = selection_index; i < AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM->number.value; i++) {
						indigo_item *item_x = AGENT_IMAGER_SELECTION_X_ITEM + 2 * i;
						indigo_item *item_y = AGENT_IMAGER_SELECTION_Y_ITEM + 2 * i;
						item_x->number.target = item_x->number.value = 0;
						item_y->number.target = item_y->number.value = 0;
					}
				}
				indigo_update_property(device, AGENT_IMAGER_SELECTION_PROPERTY, NULL);
			}
			if (AGENT_IMAGER_SELECTION_X_ITEM->number.value > 0 && AGENT_IMAGER_SELECTION_Y_ITEM->number.value > 0 && DEVICE_PRIVATE_DATA->allow_subframing) {
				select_subframe(device);
				indigo_update_property(device, AGENT_IMAGER_SELECTION_PROPERTY, NULL);
				DEVICE_PRIVATE_DATA->allow_subframing = false;
			}
			if (AGENT_IMAGER_STATS_FRAME_ITEM->number.value == 0) {
				indigo_delete_frame_digest(&DEVICE_PRIVATE_DATA->reference);
				if (indigo_selection_frame_digest(header->signature, (void*)header + sizeof(indigo_raw_header), &AGENT_IMAGER_SELECTION_X_ITEM->number.value, &AGENT_IMAGER_SELECTION_Y_ITEM->number.value, AGENT_IMAGER_SELECTION_RADIUS_ITEM->number.value, header->width, header->height, &DEVICE_PRIVATE_DATA->reference) == INDIGO_OK) {
					indigo_update_property(device, AGENT_IMAGER_SELECTION_PROPERTY, NULL);
				}
			} else {
				indigo_frame_digest digest = { 0 };
				if (indigo_selection_frame_digest(header->signature, (void*)header + sizeof(indigo_raw_header), &AGENT_IMAGER_SELECTION_X_ITEM->number.value, &AGENT_IMAGER_SELECTION_Y_ITEM->number.value, AGENT_IMAGER_SELECTION_RADIUS_ITEM->number.value, header->width, header->height, &digest) == INDIGO_OK) {
					indigo_update_property(device, AGENT_IMAGER_SELECTION_PROPERTY, NULL);
					if (indigo_calculate_drift(&DEVICE_PRIVATE_DATA->reference, &digest, &DEVICE_PRIVATE_DATA->drift_x, &DEVICE_PRIVATE_DATA->drift_y) == INDIGO_OK) {
						AGENT_IMAGER_STATS_DRIFT_X_ITEM->number.value = round(1000 * DEVICE_PRIVATE_DATA->drift_x) / 1000;
						AGENT_IMAGER_STATS_DRIFT_Y_ITEM->number.value = round(1000 * DEVICE_PRIVATE_DATA->drift_y) / 1000;
						INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Drift %.4gpx, %.4gpx", DEVICE_PRIVATE_DATA->drift_x, DEVICE_PRIVATE_DATA->drift_y);
					}
					indigo_delete_frame_digest(&digest);
				}
			}
			indigo_selection_psf(header->signature, (void*)header + sizeof(indigo_raw_header), AGENT_IMAGER_SELECTION_X_ITEM->number.value, AGENT_IMAGER_SELECTION_Y_ITEM->number.value, AGENT_IMAGER_SELECTION_RADIUS_ITEM->number.value, header->width, header->height, &AGENT_IMAGER_STATS_FWHM_ITEM->number.value, &AGENT_IMAGER_STATS_HFD_ITEM->number.value, &AGENT_IMAGER_STATS_PEAK_ITEM->number.value);
		}
	}
	if (!DEVICE_PRIVATE_DATA->frame_saturated) {
		AGENT_IMAGER_STATS_FRAME_ITEM->number.value++;
	}
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
	return INDIGO_OK_STATE;
}

static indigo_property_state capture_raw_frame(indigo_device *device, uint8_t **saturation_mask) {
	return _capture_raw_frame(device, saturation_mask, false);
}

static void preview_process(indigo_device *device) {
	FILTER_DEVICE_CONTEXT->running_process = true;
	int upload_mode = save_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_UPLOAD_MODE_PROPERTY_NAME, NULL);
	int image_format = save_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_IMAGE_FORMAT_PROPERTY_NAME, NULL);
	DEVICE_PRIVATE_DATA->use_hfd_estimator = AGENT_IMAGER_FOCUS_ESTIMATOR_HFD_PEAK_ITEM->sw.value;
	DEVICE_PRIVATE_DATA->use_ucurve_estimator = AGENT_IMAGER_FOCUS_ESTIMATOR_UCURVE_ITEM->sw.value;
	DEVICE_PRIVATE_DATA->use_rms_estimator = AGENT_IMAGER_FOCUS_ESTIMATOR_RMS_CONTRAST_ITEM->sw.value;
	AGENT_IMAGER_STATS_EXPOSURE_ITEM->number.value =
	AGENT_IMAGER_STATS_DELAY_ITEM->number.value =
	AGENT_IMAGER_STATS_FRAMES_ITEM->number.value =
	AGENT_IMAGER_STATS_FRAME_ITEM->number.value =
	AGENT_IMAGER_STATS_FWHM_ITEM->number.value =
	AGENT_IMAGER_STATS_HFD_ITEM->number.value =
	AGENT_IMAGER_STATS_PEAK_ITEM->number.value =
	AGENT_IMAGER_STATS_DRIFT_X_ITEM->number.value =
	AGENT_IMAGER_STATS_DRIFT_Y_ITEM->number.value =
	AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value = 0;
	AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value = 100;
	DEVICE_PRIVATE_DATA->allow_subframing = true;
	DEVICE_PRIVATE_DATA->find_stars = false;
	uint8_t *saturation_mask = NULL;
	allow_abort_by_mount_agent(device, false);
	disable_solver(device);
	while (capture_raw_frame(device, &saturation_mask) == INDIGO_OK_STATE);
	indigo_safe_free(saturation_mask);

	if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
		AGENT_ABORT_PROCESS_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, AGENT_ABORT_PROCESS_PROPERTY, NULL);
	}
	restore_subframe(device);
	AGENT_IMAGER_START_PREVIEW_ITEM->sw.value = AGENT_IMAGER_START_EXPOSURE_ITEM->sw.value = AGENT_IMAGER_START_STREAMING_ITEM->sw.value = AGENT_IMAGER_START_FOCUSING_ITEM->sw.value = AGENT_IMAGER_START_SEQUENCE_ITEM->sw.value = false;
	AGENT_START_PROCESS_PROPERTY->state = AGENT_IMAGER_STATS_PROPERTY->state = INDIGO_OK_STATE;
	restore_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_UPLOAD_MODE_PROPERTY_NAME, upload_mode);
	restore_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_IMAGE_FORMAT_PROPERTY_NAME, image_format);
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
	indigo_update_property(device, AGENT_START_PROCESS_PROPERTY, NULL);
	FILTER_DEVICE_CONTEXT->running_process = false;
}

static void check_breakpoint(indigo_device *device, indigo_item *breakpoint) {
	if (breakpoint->sw.value) {
		AGENT_PAUSE_PROCESS_PROPERTY->state = INDIGO_BUSY_STATE;
		AGENT_PAUSE_PROCESS_ITEM->sw.value = true;
		indigo_update_property(device, AGENT_PAUSE_PROCESS_PROPERTY, "%s paused on %s breakpoint", device->name, breakpoint->name);
		while (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
				indigo_update_property(device, AGENT_PAUSE_PROCESS_PROPERTY, "%s aborted on %s breakpoint", device->name, breakpoint->name);
				return;
			}
			if (AGENT_IMAGER_RESUME_CONDITION_BARRIER_ITEM->sw.value && DEVICE_PRIVATE_DATA->barrier_resume) {
				const char *names[] = { AGENT_PAUSE_PROCESS_ITEM_NAME };
				const bool values[] = { false };
				for (int i = 0; i < AGENT_IMAGER_BARRIER_STATE_PROPERTY->count; i++) {
					indigo_item *item = AGENT_IMAGER_BARRIER_STATE_PROPERTY->items + i;
					indigo_change_switch_property(FILTER_DEVICE_CONTEXT->client, item->name, AGENT_PAUSE_PROCESS_PROPERTY_NAME, 1, names, values);
				}
				AGENT_PAUSE_PROCESS_ITEM->sw.value = false;
				AGENT_PAUSE_PROCESS_PROPERTY->state = INDIGO_OK_STATE;
				break;
			}
			indigo_usleep(1000);
		}
		indigo_update_property(device, AGENT_PAUSE_PROCESS_PROPERTY, "%s resumed on %s breakpoint", device->name, breakpoint->name);
	}
}

static bool do_dither(indigo_device *device) {
	char *related_agent_name = indigo_filter_first_related_agent(device, "Guider Agent");
	if (!related_agent_name) {
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Dithering failed, no guider agent selected");
		indigo_send_message(device, "Dithering failed, no guider agent selected");
		return true; // do not fail batch if dithering fails - let us keep it for a while
	}
	indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, related_agent_name, AGENT_GUIDER_DITHER_PROPERTY_NAME, AGENT_GUIDER_DITHER_TRIGGER_ITEM_NAME, true);
	DEVICE_PRIVATE_DATA->dithering_started = false;
	DEVICE_PRIVATE_DATA->dithering_finished = false;
	for (int i = 0; i < 15; i++) { // wait up to 3s to start dithering
		if (DEVICE_PRIVATE_DATA->dithering_started) {
			break;
		}
		if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			return false;
		}
		indigo_usleep(200000);
	}
	if (DEVICE_PRIVATE_DATA->dithering_started) {
		AGENT_IMAGER_STATS_PHASE_ITEM->number.value = INDIGO_IMAGER_PHASE_DITHERING;
		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Dithering started");
		double time_limit = 300 * 5; // 300 * 5 * 200ms = 300s
		for (int i = 0; i < time_limit; i++) { // wait up to time limit to finish dithering
			if (DEVICE_PRIVATE_DATA->dithering_finished) {
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Dithering finished");
				break;
			}
			if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
				return false;
			}
			indigo_usleep(200000);
		}
		if (!DEVICE_PRIVATE_DATA->dithering_finished) {
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Dithering failed to settle down");
			indigo_send_message(device, "Dithering failed to settle down");
			indigo_usleep(200000);
		}
	}
	return true;
}

static bool exposure_batch(indigo_device *device) {
	double time_to_transit = DEVICE_PRIVATE_DATA->time_to_transit;
	bool pauseOnTTT = AGENT_IMAGER_PAUSE_AFTER_TRANSIT_FEATURE_ITEM->sw.value && time_to_transit < 12;
	indigo_property_state state = INDIGO_ALERT_STATE;
	indigo_property *device_exposure_property, *agent_exposure_property, *device_aux_1_exposure_property, *agent_aux_1_exposure_property, *device_frame_type_property;
	AGENT_IMAGER_STATS_EXPOSURE_ITEM->number.value = 0;
	AGENT_IMAGER_STATS_DELAY_ITEM->number.value = 0;
	AGENT_IMAGER_STATS_FRAME_ITEM->number.value = 0;
	AGENT_IMAGER_STATS_FRAMES_ITEM->number.value = AGENT_IMAGER_BATCH_COUNT_ITEM->number.target;
	AGENT_IMAGER_STATS_FRAMES_TO_DITHERING_ITEM->number.value = AGENT_IMAGER_BATCH_FRAMES_TO_SKIP_BEFORE_DITHER_ITEM->number.target;
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
	DEVICE_PRIVATE_DATA->use_aux_1 = false;
	if (indigo_filter_cached_property(device, INDIGO_FILTER_AUX_1_INDEX, CCD_EXPOSURE_PROPERTY_NAME, &device_aux_1_exposure_property, &agent_aux_1_exposure_property)) {
		DEVICE_PRIVATE_DATA->use_aux_1 = true;
	}
	if (!indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, CCD_EXPOSURE_PROPERTY_NAME, &device_exposure_property, &agent_exposure_property)) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "CCD_EXPOSURE not found");
		return false;
	}
	bool light_frame = true;
	if (indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, CCD_FRAME_TYPE_PROPERTY_NAME, &device_frame_type_property, NULL)) {
		for (int i = 0; i < device_frame_type_property->count; i++) {
			indigo_item *item = device_frame_type_property->items + i;
			if (item->sw.value) {
				light_frame = strcmp(item->name, CCD_FRAME_TYPE_LIGHT_ITEM_NAME) == 0;
				break;
			}
		}
	}
	check_breakpoint(device, AGENT_IMAGER_BREAKPOINT_PRE_BATCH_ITEM);
	set_headers(device);
	FILTER_DEVICE_CONTEXT->property_removed = false;
	// Why was it incremented here? Filter setting and focusing were considered in the previus batch or before the first batch, which makes no sense!
	// Moved it where the batch starts.
	// AGENT_IMAGER_STATS_BATCH_ITEM->number.value++;
	for (int remaining_exposures = AGENT_IMAGER_BATCH_COUNT_ITEM->number.target; remaining_exposures != 0; remaining_exposures--) {
		AGENT_IMAGER_STATS_FRAME_ITEM->number.value++;
		AGENT_IMAGER_STATS_PHASE_ITEM->number.value = INDIGO_IMAGER_PHASE_CAPTURING;
		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		if (remaining_exposures < 0)
			remaining_exposures = -1;
		check_breakpoint(device, AGENT_IMAGER_BREAKPOINT_PRE_CAPTURE_ITEM);
		for (int exposure_attempt = 0; exposure_attempt < 3; exposure_attempt++) {
			if (FILTER_DEVICE_CONTEXT->property_removed)
				return INDIGO_ALERT_STATE;
			bool pausedOnTTT = false;
			double exposure_time = AGENT_IMAGER_BATCH_EXPOSURE_ITEM->number.target;
			if (pauseOnTTT && indigo_filter_first_related_agent(device, "Mount Agent")) {
				time_to_transit = DEVICE_PRIVATE_DATA->time_to_transit;
				if (time_to_transit > 12)
					time_to_transit = time_to_transit - 24;
				if (time_to_transit <= exposure_time / 3600 - AGENT_IMAGER_BATCH_PAUSE_AFTER_TRANSIT_ITEM->number.target) {
					pauseOnTTT = false; // pause only once per batch
					AGENT_PAUSE_PROCESS_AFTER_TRANSIT_ITEM->sw.value = pausedOnTTT = true;
					AGENT_PAUSE_PROCESS_PROPERTY->state = INDIGO_BUSY_STATE;
					indigo_update_property(device, AGENT_PAUSE_PROCESS_PROPERTY, NULL);
					if (DEVICE_PRIVATE_DATA->time_to_transit >= 0) {
						indigo_send_message(device, "Batch paused, transit in %s", indigo_dtos(time_to_transit, NULL));
					} else {
						indigo_send_message(device, "Batch paused, transit %s ago", indigo_dtos(-time_to_transit, NULL));
					}
					allow_abort_by_mount_agent(device, false);
				}
			}
			while (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
				indigo_usleep(200000);
			if (pausedOnTTT) {
				allow_abort_by_mount_agent(device, true);
			}
			if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
				return false;
			if (DEVICE_PRIVATE_DATA->use_aux_1) {
				indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, device_exposure_property->device, CCD_EXPOSURE_PROPERTY_NAME, CCD_EXPOSURE_ITEM_NAME, 0);
				indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, device_aux_1_exposure_property->device, CCD_EXPOSURE_PROPERTY_NAME, CCD_EXPOSURE_ITEM_NAME, exposure_time);
			} else {
				indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, device_exposure_property->device, CCD_EXPOSURE_PROPERTY_NAME, CCD_EXPOSURE_ITEM_NAME, exposure_time);
			}
			for (int i = 0; i < BUSY_TIMEOUT * 1000 && !FILTER_DEVICE_CONTEXT->property_removed && (state = agent_exposure_property->state) != INDIGO_BUSY_STATE && AGENT_ABORT_PROCESS_PROPERTY->state != INDIGO_BUSY_STATE && AGENT_PAUSE_PROCESS_PROPERTY->state != INDIGO_BUSY_STATE; i++)
				indigo_usleep(1000);
			if (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
				while (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
					indigo_usleep(200000);
				if (AGENT_PAUSE_PROCESS_ITEM->sw.value) {
					exposure_attempt--;
					continue;
				}
			}
			if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
				return false;
			if (FILTER_DEVICE_CONTEXT->property_removed || state != INDIGO_BUSY_STATE) {
				INDIGO_DRIVER_ERROR(DRIVER_NAME, "CCD_EXPOSURE_PROPERTY didn't become busy in %d second(s)", BUSY_TIMEOUT);
				indigo_usleep(ONE_SECOND_DELAY);
				continue;
			}
			double reported_exposure_time = DEVICE_PRIVATE_DATA->use_aux_1 ?  agent_aux_1_exposure_property->items[0].number.value : agent_exposure_property->items[0].number.value;
			AGENT_IMAGER_STATS_EXPOSURE_ITEM->number.value = reported_exposure_time;
			indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
			while (!FILTER_DEVICE_CONTEXT->property_removed && (state = agent_exposure_property->state) == INDIGO_BUSY_STATE) {
				if (reported_exposure_time != agent_exposure_property->items[0].number.value) {
					AGENT_IMAGER_STATS_EXPOSURE_ITEM->number.value = reported_exposure_time = agent_exposure_property->items[0].number.value;
					indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
				}
				if (reported_exposure_time > 1) {
					indigo_usleep(200000);
				} else {
					indigo_usleep(10000);
				}
			}
			if (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
				while (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
					indigo_usleep(200000);
				if (AGENT_PAUSE_PROCESS_ITEM->sw.value) {
					exposure_attempt--;
					continue;
				}
			}
			if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
				return false;
			if (state != INDIGO_OK_STATE) {
				INDIGO_DRIVER_ERROR(DRIVER_NAME, "CCD_EXPOSURE_PROPERTY didn't become OK");
				indigo_usleep(ONE_SECOND_DELAY);
				continue;
			}
			break;
		}
		if (FILTER_DEVICE_CONTEXT->property_removed || state != INDIGO_OK_STATE) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "Exposure failed");
			return false;
		}
		check_breakpoint(device, AGENT_IMAGER_BREAKPOINT_POST_CAPTURE_ITEM);
		bool is_controlled_instance = false;
		if (!AGENT_IMAGER_RESUME_CONDITION_BARRIER_ITEM->sw.value) {
			// Do not dither if any breakpoint is set and resume condition is not set to barrier
			for (int i = 0; i < AGENT_IMAGER_BREAKPOINT_PROPERTY->count; i++) {
				if (AGENT_IMAGER_BREAKPOINT_PROPERTY->items[i].sw.value) {
					is_controlled_instance = true;
					break;
				}
			}
		}
		if (light_frame && !is_controlled_instance) {
			if (remaining_exposures != 0) {
				// AGENT_IMAGER_STATS_FRAMES_TO_DITHERING < 0 is deprecated
				if (AGENT_IMAGER_STATS_FRAMES_TO_DITHERING_ITEM->number.value >= 0 && AGENT_IMAGER_ENABLE_DITHERING_FEATURE_ITEM->sw.value && (remaining_exposures > 1 || remaining_exposures == -1 || (remaining_exposures == 1 && AGENT_IMAGER_DITHER_AFTER_BATCH_FEATURE_ITEM->sw.value))) {
					if (AGENT_IMAGER_STATS_FRAMES_TO_DITHERING_ITEM->number.value > 0) {
						AGENT_IMAGER_STATS_FRAMES_TO_DITHERING_ITEM->number.value--;
					} else {
						AGENT_IMAGER_STATS_FRAMES_TO_DITHERING_ITEM->number.value = AGENT_IMAGER_BATCH_FRAMES_TO_SKIP_BEFORE_DITHER_ITEM->number.target;
						if (!do_dither(device)) {
							return false;
						}
					}
				} else {
					AGENT_IMAGER_STATS_FRAMES_TO_DITHERING_ITEM->number.value = AGENT_IMAGER_BATCH_FRAMES_TO_SKIP_BEFORE_DITHER_ITEM->number.target;
				}
				check_breakpoint(device, AGENT_IMAGER_BREAKPOINT_PRE_DELAY_ITEM);
				double reported_delay_time = AGENT_IMAGER_BATCH_DELAY_ITEM->number.target;
				AGENT_IMAGER_STATS_DELAY_ITEM->number.value = reported_delay_time;
				AGENT_IMAGER_STATS_PHASE_ITEM->number.value = INDIGO_IMAGER_PHASE_WAITING;
				indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
				while (reported_delay_time > 0) {
					while (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
						indigo_usleep(200000);
					if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
						return false;
					if (reported_delay_time < floor(AGENT_IMAGER_STATS_DELAY_ITEM->number.value)) {
						double c = ceil(reported_delay_time);
						if (AGENT_IMAGER_STATS_DELAY_ITEM->number.value > c) {
							AGENT_IMAGER_STATS_DELAY_ITEM->number.value = c;
							indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
						}
					}
					if (reported_delay_time > 1) {
						reported_delay_time -= 0.2;
						indigo_usleep(200000);
					} else {
						reported_delay_time -= 0.01;
						indigo_usleep(10000);
					}
				}
				AGENT_IMAGER_STATS_DELAY_ITEM->number.value = 0;
				indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
				check_breakpoint(device, AGENT_IMAGER_BREAKPOINT_POST_DELAY_ITEM);
			}
		}
	}
	check_breakpoint(device, AGENT_IMAGER_BREAKPOINT_POST_BATCH_ITEM);
	return true;
}

static void exposure_batch_process(indigo_device *device) {
	FILTER_DEVICE_CONTEXT->running_process = true;
	DEVICE_PRIVATE_DATA->allow_subframing = false;
	DEVICE_PRIVATE_DATA->find_stars = false;
	AGENT_IMAGER_STATS_BATCH_ITEM->number.value = 1;
	AGENT_IMAGER_STATS_BATCHES_ITEM->number.value = 1;
	AGENT_IMAGER_STATS_BATCH_INDEX_ITEM->number.value = 0;
	DEVICE_PRIVATE_DATA->dither_num = 0;
	allow_abort_by_mount_agent(device, true);
	disable_solver(device);
	indigo_send_message(device, "Batch started");
	if (AGENT_IMAGER_RESUME_CONDITION_BARRIER_ITEM->sw.value) {
		// Start batch on related imager agents
		indigo_property *related_agents_property = FILTER_DEVICE_CONTEXT->filter_related_agent_list_property;
		for (int i = 0; i < related_agents_property->count; i++) {
			indigo_item *item = related_agents_property->items + i;
			if (item->sw.value && !strncmp(item->name, "Imager Agent", 12))
				indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, item->name, AGENT_START_PROCESS_PROPERTY_NAME, AGENT_IMAGER_START_EXPOSURE_ITEM_NAME, true);
		}
	}
	if (exposure_batch(device)) {
		AGENT_START_PROCESS_PROPERTY->state = AGENT_IMAGER_STATS_PROPERTY->state = INDIGO_OK_STATE;
		indigo_send_message(device, "Batch finished");
	} else {
		if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			AGENT_ABORT_PROCESS_PROPERTY->state = INDIGO_OK_STATE;
			indigo_update_property(device, AGENT_ABORT_PROCESS_PROPERTY, NULL);
			if (AGENT_IMAGER_BATCH_COUNT_ITEM->number.value == -1) {
				AGENT_START_PROCESS_PROPERTY->state = AGENT_IMAGER_STATS_PROPERTY->state = INDIGO_OK_STATE;
				indigo_send_message(device, "Batch finished");
			} else {
				AGENT_START_PROCESS_PROPERTY->state = INDIGO_ALERT_STATE;
				indigo_send_message(device, "Batch aborted");
			}
		} else {
			AGENT_START_PROCESS_PROPERTY->state = INDIGO_ALERT_STATE;
			indigo_send_message(device, "Batch failed");
		}
	}
	allow_abort_by_mount_agent(device, false);
	AGENT_IMAGER_START_PREVIEW_ITEM->sw.value = AGENT_IMAGER_START_EXPOSURE_ITEM->sw.value = AGENT_IMAGER_START_STREAMING_ITEM->sw.value = AGENT_IMAGER_START_FOCUSING_ITEM->sw.value = AGENT_IMAGER_START_SEQUENCE_ITEM->sw.value = false;
	AGENT_IMAGER_STATS_PHASE_ITEM->number.value = INDIGO_IMAGER_PHASE_IDLE;
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
	indigo_update_property(device, AGENT_START_PROCESS_PROPERTY, NULL);
	FILTER_DEVICE_CONTEXT->running_process = false;
}

static bool streaming_batch(indigo_device *device) {
	indigo_property_state state = INDIGO_ALERT_STATE;
	char *ccd_name = FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_CCD_INDEX];
	indigo_property *device_streaming_property, *agent_streaming_property;
	AGENT_IMAGER_STATS_EXPOSURE_ITEM->number.value = 0;
	AGENT_IMAGER_STATS_DELAY_ITEM->number.value = 0;
	AGENT_IMAGER_STATS_FRAME_ITEM->number.value = 0;
	AGENT_IMAGER_STATS_FRAMES_ITEM->number.value = AGENT_IMAGER_BATCH_COUNT_ITEM->number.target;
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
	if (!indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, CCD_STREAMING_PROPERTY_NAME, &device_streaming_property, &agent_streaming_property)) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "CCD_STREAMING not found");
		return false;
	}
	set_headers(device);
	int count_index = -1;
	for (int i = 0; i < agent_streaming_property->count; i++) {
		if (!strcmp(agent_streaming_property->items[i].name, CCD_STREAMING_COUNT_ITEM_NAME))
			count_index = i;
	}
	if (count_index == -1) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "CCD_STREAMING_COUNT_ITEM not found in CCD_STREAMING_PROPERTY");
		return false;
	}
	static char const *names[] = { AGENT_IMAGER_BATCH_COUNT_ITEM_NAME, AGENT_IMAGER_BATCH_EXPOSURE_ITEM_NAME };
	double values[] = { AGENT_IMAGER_BATCH_COUNT_ITEM->number.target, AGENT_IMAGER_BATCH_EXPOSURE_ITEM->number.target };
	indigo_change_number_property(FILTER_DEVICE_CONTEXT->client, ccd_name, CCD_STREAMING_PROPERTY_NAME, 2, names, values);
	FILTER_DEVICE_CONTEXT->property_removed = false;
	for (int i = 0; i < BUSY_TIMEOUT * 1000 && !FILTER_DEVICE_CONTEXT->property_removed && (state = agent_streaming_property->state) != INDIGO_BUSY_STATE && AGENT_ABORT_PROCESS_PROPERTY->state != INDIGO_BUSY_STATE && AGENT_PAUSE_PROCESS_PROPERTY->state != INDIGO_BUSY_STATE; i++)
		indigo_usleep(1000);
	if (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE || AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
		return false;
	if (state != INDIGO_BUSY_STATE) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "CCD_STREAMING_PROPERTY didn't become busy in %d second(s)", BUSY_TIMEOUT);
		return false;
	}
	while (!FILTER_DEVICE_CONTEXT->property_removed && (state = agent_streaming_property->state) == INDIGO_BUSY_STATE) {
		indigo_usleep(20000);
		int count = agent_streaming_property->items[count_index].number.value;
		if (count != AGENT_IMAGER_STATS_FRAME_ITEM->number.value) {
			AGENT_IMAGER_STATS_FRAME_ITEM->number.value = count;
			indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		}
	}
	if (FILTER_DEVICE_CONTEXT->property_removed || AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE || AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
		return false;
	return true;
}

static void streaming_batch_process(indigo_device *device) {
	FILTER_DEVICE_CONTEXT->running_process = true;
	DEVICE_PRIVATE_DATA->allow_subframing = false;
	DEVICE_PRIVATE_DATA->find_stars = false;
	AGENT_IMAGER_STATS_BATCH_ITEM->number.value = 1;
	AGENT_IMAGER_STATS_BATCHES_ITEM->number.value = 1;
	AGENT_IMAGER_STATS_BATCH_INDEX_ITEM->number.value = 0;
	allow_abort_by_mount_agent(device, true);
	disable_solver(device);
	indigo_send_message(device, "Streaming started");
	if (streaming_batch(device)) {
		AGENT_START_PROCESS_PROPERTY->state = AGENT_IMAGER_STATS_PROPERTY->state = INDIGO_OK_STATE;
		indigo_send_message(device, "Streaming finished");
	} else {
		if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			AGENT_ABORT_PROCESS_PROPERTY->state = INDIGO_OK_STATE;
			indigo_update_property(device, AGENT_ABORT_PROCESS_PROPERTY, NULL);
			if (AGENT_IMAGER_BATCH_COUNT_ITEM->number.value == -1) {
				AGENT_START_PROCESS_PROPERTY->state = AGENT_IMAGER_STATS_PROPERTY->state = INDIGO_OK_STATE;
				indigo_send_message(device, "Streaming finished");
			} else {
				AGENT_START_PROCESS_PROPERTY->state = INDIGO_ALERT_STATE;
				indigo_send_message(device, "Streaming aborted");
			}
		} else {
			AGENT_START_PROCESS_PROPERTY->state = INDIGO_ALERT_STATE;
			indigo_send_message(device, "Streaming failed");
		}
	}
	allow_abort_by_mount_agent(device, false);
	AGENT_IMAGER_START_PREVIEW_ITEM->sw.value = AGENT_IMAGER_START_EXPOSURE_ITEM->sw.value = AGENT_IMAGER_START_STREAMING_ITEM->sw.value = AGENT_IMAGER_START_FOCUSING_ITEM->sw.value = AGENT_IMAGER_START_SEQUENCE_ITEM->sw.value = false;
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
	indigo_update_property(device, AGENT_START_PROCESS_PROPERTY, NULL);
	FILTER_DEVICE_CONTEXT->running_process = false;
}


#define SET_BACKLASH_IF_OVERSHOOT(backlash) { \
	if ((DEVICE_PRIVATE_DATA->focuser_has_backlash) && (AGENT_IMAGER_FOCUS_BACKLASH_OVERSHOOT_ITEM->number.value > 1)) { \
		indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, focuser_name, FOCUSER_BACKLASH_PROPERTY_NAME, FOCUSER_BACKLASH_ITEM_NAME, backlash); \
	} \
}

static bool move_focuser(indigo_device *device, char *focuser_name, bool moving_out, double steps) {
	indigo_property_state state = INDIGO_ALERT_STATE;
	indigo_property *agent_steps_property;
	if (!indigo_filter_cached_property(device, INDIGO_FILTER_FOCUSER_INDEX, FOCUSER_STEPS_PROPERTY_NAME, NULL, &agent_steps_property)) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "FOCUSER_STEPS not found");
		return false;
	}
	indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, focuser_name, FOCUSER_DIRECTION_PROPERTY_NAME, moving_out ? FOCUSER_DIRECTION_MOVE_OUTWARD_ITEM_NAME : FOCUSER_DIRECTION_MOVE_INWARD_ITEM_NAME, true);
	indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, focuser_name, FOCUSER_STEPS_PROPERTY_NAME, FOCUSER_STEPS_ITEM_NAME, steps);
	for (int i = 0; i < BUSY_TIMEOUT * 1000 && !FILTER_DEVICE_CONTEXT->property_removed && (state = agent_steps_property->state) != INDIGO_BUSY_STATE && AGENT_ABORT_PROCESS_PROPERTY->state != INDIGO_BUSY_STATE; i++)
		indigo_usleep(1000);
	if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
		SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
		return false;
	}
	if (state != INDIGO_BUSY_STATE) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "FOCUSER_STEPS_PROPERTY didn't become busy in %d second(s)", BUSY_TIMEOUT);
		SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
		return false;
	}
	while (!FILTER_DEVICE_CONTEXT->property_removed && (state = agent_steps_property->state) == INDIGO_BUSY_STATE) {
		indigo_usleep(200000);
	}
	if (state != INDIGO_OK_STATE) {
		if (AGENT_ABORT_PROCESS_PROPERTY->state != INDIGO_BUSY_STATE)
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "FOCUSER_STEPS_PROPERTY didn't become OK");
		SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
		return false;
	}
	if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
		SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
		return false;
	}
	INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Moning %s %f steps", moving_out ? "OUT" : "IN", steps);
	return true;
}

static bool autofocus_overshoot(indigo_device *device, uint8_t **saturation_mask) {
	char *ccd_name = FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_CCD_INDEX];
	char *focuser_name = FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_FOCUSER_INDEX];
	AGENT_IMAGER_STATS_EXPOSURE_ITEM->number.value =
	AGENT_IMAGER_STATS_DELAY_ITEM->number.value =
	AGENT_IMAGER_STATS_FRAMES_ITEM->number.value =
	AGENT_IMAGER_STATS_FRAME_ITEM->number.value =
	AGENT_IMAGER_STATS_FWHM_ITEM->number.value =
	AGENT_IMAGER_STATS_HFD_ITEM->number.value =
	AGENT_IMAGER_STATS_PEAK_ITEM->number.value =
	AGENT_IMAGER_STATS_DRIFT_X_ITEM->number.value =
	AGENT_IMAGER_STATS_DRIFT_Y_ITEM->number.value =
	AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value = 0;
	AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value = 100;
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
	double last_quality = 0;
	double steps = AGENT_IMAGER_FOCUS_INITIAL_ITEM->number.value;
	double backlash_overshoot = AGENT_IMAGER_FOCUS_BACKLASH_OVERSHOOT_ITEM->number.value;
	double steps_todo;
	int current_offset = 0;
	DEVICE_PRIVATE_DATA->saved_backlash = AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.value;
	DEVICE_PRIVATE_DATA->use_hfd_estimator = AGENT_IMAGER_FOCUS_ESTIMATOR_HFD_PEAK_ITEM->sw.value;
	DEVICE_PRIVATE_DATA->use_ucurve_estimator = AGENT_IMAGER_FOCUS_ESTIMATOR_UCURVE_ITEM->sw.value;
	DEVICE_PRIVATE_DATA->use_rms_estimator = AGENT_IMAGER_FOCUS_ESTIMATOR_RMS_CONTRAST_ITEM->sw.value;
	int limit = DEVICE_PRIVATE_DATA->use_hfd_estimator ? AF_MOVE_LIMIT_HFD * AGENT_IMAGER_FOCUS_INITIAL_ITEM->number.value : AF_MOVE_LIMIT_RMS * AGENT_IMAGER_FOCUS_INITIAL_ITEM->number.value;
	INDIGO_DRIVER_DEBUG(DRIVER_NAME, "focuser_has_backlash = %d", DEVICE_PRIVATE_DATA->focuser_has_backlash);

	bool moving_out = true, first_move = true;
	indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, ccd_name, CCD_UPLOAD_MODE_PROPERTY_NAME, CCD_UPLOAD_MODE_CLIENT_ITEM_NAME, true);
	indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, focuser_name, FOCUSER_DIRECTION_PROPERTY_NAME, FOCUSER_DIRECTION_MOVE_OUTWARD_ITEM_NAME, true);
	SET_BACKLASH_IF_OVERSHOOT(0);
	steps_todo = steps + DEVICE_PRIVATE_DATA->saved_backlash * backlash_overshoot;

	FILTER_DEVICE_CONTEXT->property_removed = false;
	bool repeat = true;
	double  min_est = 1e10, max_est = 0;
	while (repeat) {
		if (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			while (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
				indigo_usleep(200000);
			continue;
		}
		if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
			return false;
		}
		DEVICE_PRIVATE_DATA->use_hfd_estimator = AGENT_IMAGER_FOCUS_ESTIMATOR_HFD_PEAK_ITEM->sw.value;
		DEVICE_PRIVATE_DATA->use_ucurve_estimator = AGENT_IMAGER_FOCUS_ESTIMATOR_UCURVE_ITEM->sw.value;
		DEVICE_PRIVATE_DATA->use_rms_estimator = AGENT_IMAGER_FOCUS_ESTIMATOR_RMS_CONTRAST_ITEM->sw.value;
		double quality = 0;
		int frame_count = 0;
		for (int i = 0; i < 20 && frame_count < AGENT_IMAGER_FOCUS_STACK_ITEM->number.value; i++) {
			if (capture_raw_frame(device, saturation_mask) != INDIGO_OK_STATE) {
				if (DEVICE_PRIVATE_DATA->use_rms_estimator) {
					if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
						SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
						return false;
					} else {
						continue;
					}
				} else {
					SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
					return false;
				}
			}
			indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
			if (DEVICE_PRIVATE_DATA->use_rms_estimator) {
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "RMS contrast = %f", AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value);
				if (AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value == 0) {
					continue;
				}
				quality = (quality > AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value) ? quality : AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value;
			} else if (DEVICE_PRIVATE_DATA->use_hfd_estimator) {
				if (AGENT_IMAGER_STATS_HFD_ITEM->number.value == 0 || AGENT_IMAGER_STATS_FWHM_ITEM->number.value == 0) {
					INDIGO_DRIVER_DEBUG(
						DRIVER_NAME,
						"Peak = %g, HFD = %g, FWHM = %g",
						AGENT_IMAGER_STATS_PEAK_ITEM->number.value, AGENT_IMAGER_STATS_HFD_ITEM->number.value,
						AGENT_IMAGER_STATS_FWHM_ITEM->number.value
					);
					continue;
				}
				double current_quality = AGENT_IMAGER_STATS_PEAK_ITEM->number.value / AGENT_IMAGER_STATS_HFD_ITEM->number.value;
				quality = (quality > current_quality) ? quality : current_quality;
				INDIGO_DRIVER_DEBUG(
					DRIVER_NAME,
					"Peak = %g, HFD = %g, FWHM = %g, current_quality = %g, best_quality = %g",
					AGENT_IMAGER_STATS_PEAK_ITEM->number.value,
					AGENT_IMAGER_STATS_HFD_ITEM->number.value,
					AGENT_IMAGER_STATS_FWHM_ITEM->number.value,
					current_quality,
					quality
				);
			}
			frame_count++;
		}
		if (frame_count == 0 || quality == 0) {
			indigo_send_message(device, "Failed to evaluate quality");
			continue;
		}
		if (DEVICE_PRIVATE_DATA->use_rms_estimator) {
			min_est = (min_est > quality) ? quality : min_est;
			if (DEVICE_PRIVATE_DATA->frame_saturated) {
				max_est = quality;
			} else {
				max_est = (max_est < quality) ? quality : max_est;
			}
		}
		if (DEVICE_PRIVATE_DATA->use_hfd_estimator) {
			min_est = (min_est > AGENT_IMAGER_STATS_HFD_ITEM->number.value) ? AGENT_IMAGER_STATS_HFD_ITEM->number.value : min_est;
			max_est = (max_est < AGENT_IMAGER_STATS_HFD_ITEM->number.value) ? AGENT_IMAGER_STATS_HFD_ITEM->number.value : max_est;
		}
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Focus Quality = %g %s", quality, DEVICE_PRIVATE_DATA->frame_saturated ? "(saturated)" : "");
		if (quality >= last_quality && abs(current_offset) < limit) {
			if (moving_out) {
				steps_todo = steps + DEVICE_PRIVATE_DATA->saved_backlash * backlash_overshoot;
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Moving out %d + %d = %d steps", (int)steps, (int)(steps_todo - steps), (int)steps_todo);
				current_offset += steps;
			} else {
				steps_todo = steps;
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Moving in %d + %d = %d steps", (int)steps, (int)(steps_todo - steps), (int)steps_todo);
				current_offset -= steps;
			}
			if (!move_focuser(device, focuser_name, moving_out, steps_todo)) break;
		} else if (steps <= AGENT_IMAGER_FOCUS_FINAL_ITEM->number.value || abs(current_offset) >= limit) {
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Current_offset %d steps", (int)current_offset);
			if (
				(AGENT_IMAGER_STATS_HFD_ITEM->number.value > 1.2 * AGENT_IMAGER_SELECTION_RADIUS_ITEM->number.value && DEVICE_PRIVATE_DATA->use_hfd_estimator) ||
				(abs(current_offset) >= limit && DEVICE_PRIVATE_DATA->use_rms_estimator)
			) {
				break;
			} else {
				moving_out = !moving_out;
				if (moving_out) {
					steps_todo = steps + DEVICE_PRIVATE_DATA->saved_backlash * backlash_overshoot;
					INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Switching and moving out %d + %d = %d steps to final position", (int)steps, (int)(steps_todo - steps), (int)steps_todo);
					current_offset += steps;
					if (!move_focuser(device, focuser_name, moving_out, steps_todo))
						break;
				} else {
					steps_todo = steps;
					INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Switching and moving in %d + %d = %d steps to final position", (int)steps, (int)(steps_todo - steps), (int)steps_todo);
					current_offset -= steps;
					if (!move_focuser(device, focuser_name, moving_out, steps_todo))
						break;
				}
			}
			repeat = false;
		} else {
			moving_out = !moving_out;
			if (!first_move) {
				steps = round(steps / 2);
				if (steps < 1)
					steps = 1;
			}
			first_move = false;
			if (moving_out) {
				steps_todo = steps + DEVICE_PRIVATE_DATA->saved_backlash * backlash_overshoot;
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Switching and moving out %d + %d = %d steps", (int)steps, (int)(steps_todo - steps), (int)steps_todo);
				current_offset += steps;
				if (!move_focuser(device, focuser_name, moving_out, steps_todo))
					break;
			} else {
				steps_todo = steps;
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Switching and moving in %d + %d = %d steps", (int)steps, (int)(steps_todo - steps), (int)steps_todo);
				current_offset -= steps;
				if (!move_focuser(device, focuser_name, moving_out, steps_todo))
					break;
			}
		}
		AGENT_IMAGER_STATS_FOCUS_OFFSET_ITEM->number.value = current_offset;
		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		if (DEVICE_PRIVATE_DATA->saved_backlash * backlash_overshoot > 0 && moving_out) {
			double steps_todo = DEVICE_PRIVATE_DATA->saved_backlash * backlash_overshoot;
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Overshot by %d steps, compensating", (int)steps_todo);
			if (!move_focuser(device, focuser_name, false, steps_todo))
				break;
		} else if (moving_out) {
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "No overshoot, compensation skipped");
		}
		last_quality = quality;
	}
	capture_raw_frame(device, saturation_mask);
	if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
		SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
		return false;
	}
	// Calculate focus deviation from best
	if (DEVICE_PRIVATE_DATA->use_rms_estimator) {
		if (max_est > min_est) {
			AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value = 100 * (max_est - AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value) / (max_est - min_est);
		} else {
			AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value = 100;
		}
	}
	if (DEVICE_PRIVATE_DATA->use_hfd_estimator) {
		if (min_est > 0) {
			AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value = 100 * (min_est - AGENT_IMAGER_STATS_HFD_ITEM->number.value) / min_est;
		} else {
			AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value = 100;
		}
	}
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);

	bool focus_failed = false;
	if (abs(current_offset) >= limit) {
		indigo_send_message(device, "No focus reached within maximum travel limit per AF run");
		focus_failed = true;
	} else if (AGENT_IMAGER_STATS_HFD_ITEM->number.value > 1.2 * AGENT_IMAGER_SELECTION_RADIUS_ITEM->number.value && DEVICE_PRIVATE_DATA->use_hfd_estimator) {
		indigo_send_message(device, "No focus reached, did not converge");
		focus_failed = true;
	} else if (
		(DEVICE_PRIVATE_DATA->use_hfd_estimator && AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value > 15) || /* for HFD 15% deviation is ok - tested on realsky */
		(DEVICE_PRIVATE_DATA->use_rms_estimator && AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value > 25)    /* for RMS 25% deviation is ok - tested on realsky */
	) {
		indigo_send_message(device, "Focus does not meet the quality criteria");
		focus_failed = true;
	}

	if (focus_failed) {
		if (DEVICE_PRIVATE_DATA->restore_initial_position) {
			indigo_send_message(device, "Focus failed, restoring initial position");
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Focus failed, moving to initial position %d steps", (int)current_offset);
			if (current_offset > 0) {
				moving_out = false;
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Focus failed, moving in to initial position %d steps", (int)current_offset);
				move_focuser(device, focuser_name, false, current_offset);
			} else if (current_offset < 0) {
				moving_out = true;
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Focus failed, moving out to initial position %d + %d = steps", -(int)current_offset, (int)(DEVICE_PRIVATE_DATA->saved_backlash * backlash_overshoot), -(int)current_offset + (int)(DEVICE_PRIVATE_DATA->saved_backlash * backlash_overshoot));
				current_offset = -current_offset + DEVICE_PRIVATE_DATA->saved_backlash * backlash_overshoot;
				move_focuser(device, focuser_name, true, current_offset);
			}
			current_offset = 0;
			if (DEVICE_PRIVATE_DATA->saved_backlash * backlash_overshoot > 0 && moving_out) {
				double steps_todo = DEVICE_PRIVATE_DATA->saved_backlash * backlash_overshoot;
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Overshot by %d steps, compensating", (int)steps_todo);
				move_focuser(device, focuser_name, false, steps_todo);
			} else if (moving_out) {
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "No overshoot, compensation skipped");
			}
		}
		SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
		return false;
	} else {
		SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
		return true;
	}
}

static bool autofocus_ucurve(indigo_device *device) {
	indigo_client *client = FILTER_DEVICE_CONTEXT->client;
	char *ccd_name = FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_CCD_INDEX];
	char *focuser_name = FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_FOCUSER_INDEX];
	AGENT_IMAGER_STATS_EXPOSURE_ITEM->number.value =
	AGENT_IMAGER_STATS_DELAY_ITEM->number.value =
	AGENT_IMAGER_STATS_FRAMES_ITEM->number.value =
	AGENT_IMAGER_STATS_FRAME_ITEM->number.value =
	AGENT_IMAGER_STATS_FWHM_ITEM->number.value =
	AGENT_IMAGER_STATS_HFD_ITEM->number.value =
	AGENT_IMAGER_STATS_PEAK_ITEM->number.value =
	AGENT_IMAGER_STATS_DRIFT_X_ITEM->number.value =
	AGENT_IMAGER_STATS_DRIFT_Y_ITEM->number.value =
	AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value = 0;
	AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value = 100;
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
	double last_quality = 0, min_est = 1e10;
	double steps = AGENT_IMAGER_FOCUS_INITIAL_ITEM->number.value;
	double backlash_overshoot = AGENT_IMAGER_FOCUS_BACKLASH_OVERSHOOT_ITEM->number.value;
	int current_offset = 0;

	DEVICE_PRIVATE_DATA->ucurve_samples_number = (int)rint(AGENT_IMAGER_FOCUS_UCURVE_SAMPLES_ITEM->number.value);
	DEVICE_PRIVATE_DATA->saved_backlash = AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.value;
	DEVICE_PRIVATE_DATA->use_hfd_estimator = AGENT_IMAGER_FOCUS_ESTIMATOR_HFD_PEAK_ITEM->sw.value;
	DEVICE_PRIVATE_DATA->use_ucurve_estimator = AGENT_IMAGER_FOCUS_ESTIMATOR_UCURVE_ITEM->sw.value;
	DEVICE_PRIVATE_DATA->use_rms_estimator = AGENT_IMAGER_FOCUS_ESTIMATOR_RMS_CONTRAST_ITEM->sw.value;

	int limit = AF_MOVE_LIMIT_UCURVE * AGENT_IMAGER_FOCUS_INITIAL_ITEM->number.value * DEVICE_PRIVATE_DATA->ucurve_samples_number;
	bool moving_out = true;
	int sample = 0;
	double best_value = 0;
	int best_index = 0;
	bool focus_far_enough = false;
	indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, ccd_name, CCD_UPLOAD_MODE_PROPERTY_NAME, CCD_UPLOAD_MODE_CLIENT_ITEM_NAME, true);
	indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, focuser_name, FOCUSER_DIRECTION_PROPERTY_NAME, FOCUSER_DIRECTION_MOVE_OUTWARD_ITEM_NAME, true);
	SET_BACKLASH_IF_OVERSHOOT(0);

	INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: focuser_has_backlash = %d", DEVICE_PRIVATE_DATA->focuser_has_backlash);

	FILTER_DEVICE_CONTEXT->property_removed = false;
	bool repeat = true;
	bool focus_failed = false;
	double hfds[MAX_UCURVE_SAMPLES] = {0};
	double focus_pos[MAX_UCURVE_SAMPLES] = {0};
	while (repeat) {
		if (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			while (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
				indigo_usleep(200000);
			continue;
		}
		if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
			return false;
		}
		double quality = 0;
		int frame_count = 0;
		for (int i = 0; i < 20 && frame_count < AGENT_IMAGER_FOCUS_STACK_ITEM->number.value; i++) {
			if (capture_raw_frame(device, NULL) != INDIGO_OK_STATE) {
				if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
					SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
					return false;
				} else {
					continue;
				}
			}
			indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
			if (DEVICE_PRIVATE_DATA->use_ucurve_estimator) {
				if (AGENT_IMAGER_STATS_HFD_ITEM->number.value == 0 || AGENT_IMAGER_STATS_FWHM_ITEM->number.value == 0) {
					INDIGO_DRIVER_DEBUG(
						DRIVER_NAME,
						"UC: Peak = %g, HFD = %g, FWHM = %g",
						AGENT_IMAGER_STATS_PEAK_ITEM->number.value,
						AGENT_IMAGER_STATS_HFD_ITEM->number.value,
						AGENT_IMAGER_STATS_FWHM_ITEM->number.value
					);
					continue;
				}
				double current_quality = 1 / AGENT_IMAGER_STATS_HFD_ITEM->number.value;
				quality = (quality > current_quality) ? quality : current_quality;
				INDIGO_DRIVER_DEBUG(
					DRIVER_NAME,
					"UC: Peak = %g, HFD = %g, FWHM = %g, current_quality = %g, best_quality = %g",
					AGENT_IMAGER_STATS_PEAK_ITEM->number.value,
					AGENT_IMAGER_STATS_HFD_ITEM->number.value,
					AGENT_IMAGER_STATS_FWHM_ITEM->number.value,
					current_quality,
					quality
				);
			} else {
				INDIGO_DRIVER_ERROR(DRIVER_NAME, "BUG: This should not happen - Only U-CURVE estimator is supported for this function");
				SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
				return false;
			}
			frame_count++;
		}
		if (frame_count == 0 || quality == 0) {
			indigo_send_message(device, "Failed to evaluate quality");
			continue;
		}

		if (DEVICE_PRIVATE_DATA->use_ucurve_estimator) {
			min_est = (min_est > AGENT_IMAGER_STATS_HFD_ITEM->number.value) ? AGENT_IMAGER_STATS_HFD_ITEM->number.value : min_est;
		} else {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "BUG: This should not happen - Only U-CURVE estimator is supported for this function");
			SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
			return false;
		}
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: Focus Quality = %g (Previous %g)", quality, last_quality);

		if (sample == 0) {
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: First sample");
			if (!move_focuser(device, focuser_name, moving_out, steps)) break;
			if(moving_out) {
				current_offset += steps;
			} else {
				current_offset -= steps;
			}
		} else if (sample == 1) {
			focus_pos[sample-1] = CLIENT_PRIVATE_DATA->focuser_position;
			hfds[sample-1] = AGENT_IMAGER_STATS_HFD_ITEM->number.value;
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: pos[%d] = (%g, %f)", sample-1, focus_pos[sample-1], hfds[sample-1]);
			if (last_quality >= quality) {
				moving_out = false;
				if (!move_focuser(device, focuser_name, moving_out, steps)) break;
				current_offset -= steps;
			} else {
				moving_out = true;
				if (!move_focuser(device, focuser_name, moving_out, steps)) break;
				current_offset += steps;
			}
		} else {
			int midpoint = rint(DEVICE_PRIVATE_DATA->ucurve_samples_number / 2.0);
			if (sample > midpoint && last_quality <= quality) {
				/* We've traversed through half of the samples without encountering the optimal one - it's necessary
				   to move all samples to the left and continue the search. This is a common situation when the best
				   focus is outside the initial window.
				 */
				for (int i = 0; i < sample-1; i++) {
					focus_pos[i] = focus_pos[i+1];
					hfds[i] = hfds[i+1];
				}
				sample = midpoint;
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: Did not reach best focus - shifting samples to the left");
			}
			focus_pos[sample-1] = CLIENT_PRIVATE_DATA->focuser_position;
			hfds[sample-1] = AGENT_IMAGER_STATS_HFD_ITEM->number.value;
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: pos[%d] = (%g, %f)", sample-1, focus_pos[sample-1], hfds[sample-1]);

			/* If we've crossed the halfway point, we identify the optimal value index up to this point.
			   If it's sufficiently distant, we can begin moving towards the best focus and the result
			   will be a well-balanced U-Curve.
			 */
			if (sample > midpoint + 2 && !focus_far_enough) {
				int window_offset = sample - midpoint - 2;
				int window_lenght = midpoint + 2;
				double *window_base = hfds + window_offset;
				best_value = window_base[0];
				best_index = 0;
				for(int i = 0; i < window_lenght; i++) {
					if (window_base[i] < best_value) {
						best_value = window_base[i];
						best_index = i;
					}
				}
				/* If we are at a sufficient distance from the optimal focus, we can begin to move towards it,
				   resulting in a symmetric U-Curve.
				 */
				if (best_index == 0) {
					if (indigo_get_log_level() >= INDIGO_LOG_DEBUG) {
						INDIGO_DRIVER_DEBUG(
							DRIVER_NAME,
							"UC: The best focus is outside the window hfds[%d, %d] - starting approach",
							window_offset, window_lenght-1
						);

						INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: Currently collected %d samples:", sample);
						for (int i = 0; i < sample; i++) {
							INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: point[%d] = (%g, %f)", i, focus_pos[i], hfds[i]);
						}

						INDIGO_DRIVER_DEBUG(
							DRIVER_NAME,
							"UC: sample = %d, midpoint = %d, best_index = %d, best_value = %f",
							sample, midpoint, best_index, best_value
						);
					}

					sample = 0;
					moving_out = true;
					focus_far_enough = true;
					AGENT_IMAGER_STATS_FOCUS_OFFSET_ITEM->number.value = current_offset;
					indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
					continue;
				}
			}

			if (sample == DEVICE_PRIVATE_DATA->ucurve_samples_number) {
				best_value = hfds[0];
				best_index = 0;
				for(int i = 0; i < DEVICE_PRIVATE_DATA->ucurve_samples_number; i++) {
					if (hfds[i] < best_value) {
						best_value = hfds[i];
						best_index = i;
					}
				}
				/* If the optimal focus is not close enough to center,
				   we need restart the process to get a symmetric U-Curve.
				 */
				if (abs(best_index - midpoint) > 1) {
					sample = 0;
					moving_out = true;
					INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: The best_index = %d is far from the midpoint = %d - rerunning", best_index, midpoint);
					AGENT_IMAGER_STATS_FOCUS_OFFSET_ITEM->number.value = current_offset;
					indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
					continue;
				} else {
					repeat = false;
				}
			} else {
				if (!move_focuser(device, focuser_name, moving_out, steps)) break;
				if (moving_out) {
					current_offset += steps;
				} else {
					current_offset -= steps;
				}
			}
		}
		sample++;
		AGENT_IMAGER_STATS_FOCUS_OFFSET_ITEM->number.value = current_offset;
		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		last_quality = quality;
		if (abs(current_offset) >= limit) {
			indigo_send_message(device, "No focus reached within maximum travel limit per AF run");
			focus_failed = true;
			goto ucurve_finish;
		}
	}

	if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
		SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
		return false;
	}

	double polynomial[UCURVE_ORDER + 1];
	double best_focus = 0;

	int res = indigo_polynomial_fit(DEVICE_PRIVATE_DATA->ucurve_samples_number, focus_pos, hfds, UCURVE_ORDER + 1, polynomial);
	if (res < 0) {
		indigo_send_message(device, "U-Curve failed to fit data points with polynomial");
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to fit polynomial");
		SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
		focus_failed = true;
		goto ucurve_finish;
	}

	if (indigo_get_log_level() >= INDIGO_LOG_DEBUG) {
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: U-Curve collected %d samples (final set):", DEVICE_PRIVATE_DATA->ucurve_samples_number);
		for (int i = 0; i < DEVICE_PRIVATE_DATA->ucurve_samples_number; i++) {
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: point[%d] = (%g, %f)", i, focus_pos[i], hfds[i]);
		}
		char polynomial_str[1204];
		indigo_polinomial_string(UCURVE_ORDER + 1, polynomial, polynomial_str);
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: Polynomial fit: %s", polynomial_str);
	}

	if (focus_pos[0] < focus_pos[DEVICE_PRIVATE_DATA->ucurve_samples_number - 1]) {
		best_focus = indigo_polynomial_min_x(UCURVE_ORDER + 1, polynomial, focus_pos[0], focus_pos[DEVICE_PRIVATE_DATA->ucurve_samples_number - 1], 0.00001);
		if (best_focus < focus_pos[1] || best_focus > focus_pos[DEVICE_PRIVATE_DATA->ucurve_samples_number - 2]) {
			focus_failed = true;
		}
	} else {
		best_focus = indigo_polynomial_min_x(UCURVE_ORDER + 1, polynomial, focus_pos[DEVICE_PRIVATE_DATA->ucurve_samples_number - 1], focus_pos[0], 0.00001);
		if (best_focus < focus_pos[DEVICE_PRIVATE_DATA->ucurve_samples_number - 2] || best_focus > focus_pos[1]) {
			focus_failed = true;
		}
	}

	if (focus_failed) {
		indigo_send_message(device, "U-Curve failed to find best focus position in the acceptable range");
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to find best focus position in the acceptable range");
		SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
		goto ucurve_finish;
	}

	/* Calculate the steps to best focus */
	double steps_to_focus = fabs(CLIENT_PRIVATE_DATA->focuser_position - best_focus);

	indigo_send_message(device, "U-Curve found best focus at position %.3f", best_focus);
	INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: U-Curve found best focus at position %.3f, steps_to_focus = %g", best_focus, steps_to_focus);

	/* Apply backlash or overshoot if needed */
	if (backlash_overshoot > 1 && DEVICE_PRIVATE_DATA->saved_backlash > 0) {
		steps_to_focus += DEVICE_PRIVATE_DATA->saved_backlash * backlash_overshoot;
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: Applying overshoot: steps_to_focus = %f including overshoot = %f", steps_to_focus, DEVICE_PRIVATE_DATA->saved_backlash * backlash_overshoot);
	} else if (!DEVICE_PRIVATE_DATA->focuser_has_backlash) { /* the focuser driver has no backlash, so we take care of it */
		steps_to_focus += AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.value + AGENT_IMAGER_FOCUS_BACKLASH_OUT_ITEM->number.value;
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: Applying backlash: steps_to_focus = %f including backlash = %f", steps_to_focus, AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.value + AGENT_IMAGER_FOCUS_BACKLASH_OUT_ITEM->number.value);
	}

	/* Move to best focus position */
	if (!move_focuser(device, focuser_name, !moving_out, steps_to_focus)) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to move to best focus position");
		SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
		return false;
	}
	if (!moving_out) {
		current_offset += steps_to_focus;
	} else {
		current_offset -= steps_to_focus;
	}

	/* Compensate for the overshoot if applied */
	if (backlash_overshoot > 1 && DEVICE_PRIVATE_DATA->saved_backlash > 0) {
		steps_to_focus = DEVICE_PRIVATE_DATA->saved_backlash * backlash_overshoot;
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "UC: Compensating overshoot: overshoot = %f", steps_to_focus);
		if (!move_focuser(device, focuser_name, moving_out, steps_to_focus)) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to apply overshoot");
			SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
			return false;
		} else {
			if(moving_out) {
				current_offset += steps_to_focus;
			} else {
				current_offset -= steps_to_focus;
			}
		}
	}

	capture_raw_frame(device, NULL);
	if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
		SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
		return false;
	}
	/* Calculate focus deviation from the optimal one */
	if (DEVICE_PRIVATE_DATA->use_ucurve_estimator) {
		if (min_est > 0) {
			AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value = 100 * (min_est - AGENT_IMAGER_STATS_HFD_ITEM->number.value) / min_est;
		} else {
			AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value = 100;
		}
	} else {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "BUG: This should not happen - Only U-CURVE estimator is supported for this function");
		SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
		return false;
	}
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);

	if (AGENT_IMAGER_STATS_HFD_ITEM->number.value > 1.2 * AGENT_IMAGER_SELECTION_RADIUS_ITEM->number.value && DEVICE_PRIVATE_DATA->use_hfd_estimator) {
		indigo_send_message(device, "No focus reached, did not converge");
		focus_failed = true;
	} else if (
		(DEVICE_PRIVATE_DATA->use_hfd_estimator && AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value > 15) /* for HFD 15% deviation is ok - tested on realsky */
	) {
		indigo_send_message(device, "Focus does not meet the quality criteria");
		focus_failed = true;
	}

	ucurve_finish:
	if (focus_failed) {
		if (DEVICE_PRIVATE_DATA->restore_initial_position) {
			indigo_send_message(device, "Focus failed, restoring initial position");
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to reach focus, moving to initial position %d steps", (int)current_offset);
			if (current_offset > 0) {
				if (moving_out && !DEVICE_PRIVATE_DATA->focuser_has_backlash) {
					current_offset += AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.value + AGENT_IMAGER_FOCUS_BACKLASH_OUT_ITEM->number.value;
				}
				move_focuser(device, focuser_name, false, current_offset);
			} else if (current_offset < 0) {
				current_offset = -current_offset;
				if (!moving_out && !DEVICE_PRIVATE_DATA->focuser_has_backlash) {
					current_offset += AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.value + AGENT_IMAGER_FOCUS_BACKLASH_IN_ITEM->number.value;
				}
				move_focuser(device, focuser_name, true, current_offset);
			}
			current_offset = 0;
		}
		SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
		return false;
	} else {
		SET_BACKLASH_IF_OVERSHOOT(DEVICE_PRIVATE_DATA->saved_backlash);
		return true;
	}
}

static bool autofocus_backlash(indigo_device *device, uint8_t **saturation_mask) {
	char *ccd_name = FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_CCD_INDEX];
	char *focuser_name = FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_FOCUSER_INDEX];
	AGENT_IMAGER_STATS_EXPOSURE_ITEM->number.value =
	AGENT_IMAGER_STATS_DELAY_ITEM->number.value =
	AGENT_IMAGER_STATS_FRAMES_ITEM->number.value =
	AGENT_IMAGER_STATS_FRAME_ITEM->number.value =
	AGENT_IMAGER_STATS_FWHM_ITEM->number.value =
	AGENT_IMAGER_STATS_HFD_ITEM->number.value =
	AGENT_IMAGER_STATS_PEAK_ITEM->number.value =
	AGENT_IMAGER_STATS_DRIFT_X_ITEM->number.value =
	AGENT_IMAGER_STATS_DRIFT_Y_ITEM->number.value =
	AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value = 0;
	AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value = 100;
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
	double last_quality = 0, min_est = 1e10, max_est = 0;
	double steps = AGENT_IMAGER_FOCUS_INITIAL_ITEM->number.value;
	double steps_todo;
	int current_offset = 0;
	int limit = DEVICE_PRIVATE_DATA->use_hfd_estimator ? AF_MOVE_LIMIT_HFD * AGENT_IMAGER_FOCUS_INITIAL_ITEM->number.value : AF_MOVE_LIMIT_RMS * AGENT_IMAGER_FOCUS_INITIAL_ITEM->number.value;
	INDIGO_DRIVER_DEBUG(DRIVER_NAME, "focuser_has_backlash = %d", DEVICE_PRIVATE_DATA->focuser_has_backlash);
	if (DEVICE_PRIVATE_DATA->focuser_has_backlash) { /* the focuser driver has a backlash, so it will take care of it */
		steps_todo = steps;
	} else {
		steps_todo = steps + AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.value + AGENT_IMAGER_FOCUS_BACKLASH_OUT_ITEM->number.value;
	}
	bool moving_out = true, first_move = true;
	indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, ccd_name, CCD_UPLOAD_MODE_PROPERTY_NAME, CCD_UPLOAD_MODE_CLIENT_ITEM_NAME, true);
	indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, focuser_name, FOCUSER_DIRECTION_PROPERTY_NAME, FOCUSER_DIRECTION_MOVE_OUTWARD_ITEM_NAME, true);

	FILTER_DEVICE_CONTEXT->property_removed = false;
	bool repeat = true;
	while (repeat) {
		if (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			while (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
				indigo_usleep(200000);
			continue;
		}
		if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			return false;
		}
		DEVICE_PRIVATE_DATA->use_hfd_estimator = AGENT_IMAGER_FOCUS_ESTIMATOR_HFD_PEAK_ITEM->sw.value;
		DEVICE_PRIVATE_DATA->use_ucurve_estimator = AGENT_IMAGER_FOCUS_ESTIMATOR_UCURVE_ITEM->sw.value;
		DEVICE_PRIVATE_DATA->use_rms_estimator = AGENT_IMAGER_FOCUS_ESTIMATOR_RMS_CONTRAST_ITEM->sw.value;
		double quality = 0;
		int frame_count = 0;
		for (int i = 0; i < 20 && frame_count < AGENT_IMAGER_FOCUS_STACK_ITEM->number.value; i++) {
			if (capture_raw_frame(device, saturation_mask) != INDIGO_OK_STATE) {
				if (DEVICE_PRIVATE_DATA->use_rms_estimator) {
					if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
						return false;
					} else {
						continue;
					}
				} else {
					return false;
				}
			}
			indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
			if (DEVICE_PRIVATE_DATA->use_rms_estimator) {
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "RMS contrast = %f", AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value);
				if (AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value == 0) {
					continue;
				}
				quality = (quality > AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value) ? quality : AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value;
			} else if (DEVICE_PRIVATE_DATA->use_hfd_estimator) {
				if (AGENT_IMAGER_STATS_HFD_ITEM->number.value == 0 || AGENT_IMAGER_STATS_FWHM_ITEM->number.value == 0) {
					INDIGO_DRIVER_DEBUG(
						DRIVER_NAME,
						"Peak = %g, HFD = %g, FWHM = %g",
						AGENT_IMAGER_STATS_PEAK_ITEM->number.value,
						AGENT_IMAGER_STATS_HFD_ITEM->number.value,
						AGENT_IMAGER_STATS_FWHM_ITEM->number.value
					);
					continue;
				}
				double current_quality = AGENT_IMAGER_STATS_PEAK_ITEM->number.value / AGENT_IMAGER_STATS_HFD_ITEM->number.value;
				quality = (quality > current_quality) ? quality : current_quality;
				INDIGO_DRIVER_DEBUG(
					DRIVER_NAME,
					"Peak = %g, HFD = %g, FWHM = %g, current_quality = %g, best_quality = %g",
					AGENT_IMAGER_STATS_PEAK_ITEM->number.value,
					AGENT_IMAGER_STATS_HFD_ITEM->number.value,
					AGENT_IMAGER_STATS_FWHM_ITEM->number.value,
					current_quality,
					quality
				);
			}
			frame_count++;
		}
		if (frame_count == 0 || quality == 0) {
			indigo_send_message(device, "Failed to evaluate quality");
			continue;
		}
		if (DEVICE_PRIVATE_DATA->use_rms_estimator) {
			min_est = (min_est > quality) ? quality : min_est;
			if (DEVICE_PRIVATE_DATA->frame_saturated) {
				max_est = quality;
			} else {
				max_est = (max_est < quality) ? quality : max_est;
			}
		}
		if (DEVICE_PRIVATE_DATA->use_hfd_estimator) {
			min_est = (min_est > AGENT_IMAGER_STATS_HFD_ITEM->number.value) ? AGENT_IMAGER_STATS_HFD_ITEM->number.value : min_est;
			max_est = (max_est < AGENT_IMAGER_STATS_HFD_ITEM->number.value) ? AGENT_IMAGER_STATS_HFD_ITEM->number.value : max_est;
		}
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Focus Quality = %g %s", quality, DEVICE_PRIVATE_DATA->frame_saturated ? "(saturated)" : "");
		if (quality >= last_quality && abs(current_offset) < limit) {
			if (moving_out) {
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Moving out %d steps", (int)steps);
				current_offset += steps;
			} else {
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Moving in %d steps", (int)steps);
				current_offset -= steps;
			}
			if (!move_focuser(device, focuser_name, moving_out, steps))
				break;
		} else if (steps <= AGENT_IMAGER_FOCUS_FINAL_ITEM->number.value || abs(current_offset) >= limit) {
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Current_offset %d steps", (int)current_offset);
			if (
				(AGENT_IMAGER_STATS_HFD_ITEM->number.value > 1.2 * AGENT_IMAGER_SELECTION_RADIUS_ITEM->number.value && DEVICE_PRIVATE_DATA->use_hfd_estimator) ||
				(abs(current_offset) >= limit && DEVICE_PRIVATE_DATA->use_rms_estimator)
			) {
				break;
			} else {
				moving_out = !moving_out;
				if (moving_out) {
					INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Switching and moving out %d steps to final position", (int)steps_todo);
					current_offset += steps;
				} else {
					INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Switching and moving in %d steps to final position", (int)steps_todo);
					current_offset -= steps;
				}
				if (!move_focuser(device, focuser_name, moving_out, steps_todo))
					break;
			}
			repeat = false;
		} else {
			moving_out = !moving_out;
			if (!first_move) {
				steps = round(steps / 2);
				if (steps < 1)
					steps = 1;
				if (DEVICE_PRIVATE_DATA->focuser_has_backlash) { /* the focuser driver has a backlash, so it will take care of it */
					steps_todo = steps;
				} else {
					steps_todo = steps + AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.value + (moving_out ? AGENT_IMAGER_FOCUS_BACKLASH_OUT_ITEM->number.value : AGENT_IMAGER_FOCUS_BACKLASH_IN_ITEM->number.value);
				}
			}
			first_move = false;
			if (moving_out) {
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Switching and moving out %d steps", (int)steps_todo);
				current_offset += steps;
			} else {
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Switching and moving in %d steps", (int)steps_todo);
				current_offset -= steps;
			}
			if (!move_focuser(device, focuser_name, moving_out, steps_todo))
				break;
		}
		AGENT_IMAGER_STATS_FOCUS_OFFSET_ITEM->number.value = current_offset;
		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		last_quality = quality;
	}
	capture_raw_frame(device, saturation_mask);
	if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
		return false;
	}
	// Calculate focus deviation from best
	if (DEVICE_PRIVATE_DATA->use_rms_estimator) {
		if (max_est > min_est) {
			AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value = 100 * (max_est - AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM->number.value) / (max_est - min_est);
		} else {
			AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value = 100;
		}
	}
	if (DEVICE_PRIVATE_DATA->use_hfd_estimator) {
		if (min_est > 0) {
			AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value = 100 * (min_est - AGENT_IMAGER_STATS_HFD_ITEM->number.value) / min_est;
		} else {
			AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value = 100;
		}
	}
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);

	bool focus_failed = false;
	if (abs(current_offset) >= limit) {
		indigo_send_message(device, "No focus reached within maximum travel limit per AF run");
		focus_failed = true;
	} else if (AGENT_IMAGER_STATS_HFD_ITEM->number.value > 1.2 * AGENT_IMAGER_SELECTION_RADIUS_ITEM->number.value && DEVICE_PRIVATE_DATA->use_hfd_estimator) {
		indigo_send_message(device, "No focus reached, did not converge");
		focus_failed = true;
	} else if (
		(DEVICE_PRIVATE_DATA->use_hfd_estimator && AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value > 15) || /* for HFD 15% deviation is ok - tested on realsky */
		(DEVICE_PRIVATE_DATA->use_rms_estimator && AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM->number.value > 25)    /* for RMS 25% deviation is ok - tested on realsky */
	) {
		indigo_send_message(device, "Focus does not meet the quality criteria");
		focus_failed = true;
	}

	if (focus_failed) {
		if (DEVICE_PRIVATE_DATA->restore_initial_position) {
			indigo_send_message(device, "Focus failed, restoring initial position");
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to reach focus, moving to initial position %d steps", (int)current_offset);
			if (current_offset > 0) {
				if (moving_out && !DEVICE_PRIVATE_DATA->focuser_has_backlash) {
					current_offset += AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.value + AGENT_IMAGER_FOCUS_BACKLASH_OUT_ITEM->number.value;
				}
				move_focuser(device, focuser_name, false, current_offset);
			} else if (current_offset < 0) {
				current_offset = -current_offset;
				if (!moving_out && !DEVICE_PRIVATE_DATA->focuser_has_backlash) {
					current_offset += AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.value + AGENT_IMAGER_FOCUS_BACKLASH_IN_ITEM->number.value;
				}
				move_focuser(device, focuser_name, true, current_offset);
			}
			current_offset = 0;
		}
		return false;
	} else {
		return true;
	}
}

static bool autofocus(indigo_device *device) {
	bool result;
	uint8_t *saturation_mask = NULL;
	if (AGENT_IMAGER_FOCUS_ESTIMATOR_UCURVE_ITEM->sw.value) {
		result = autofocus_ucurve(device);
	} else if (AGENT_IMAGER_FOCUS_BACKLASH_OVERSHOOT_ITEM->number.value > 1) {
		result = autofocus_overshoot(device, &saturation_mask);
	} else {
		result = autofocus_backlash(device, &saturation_mask);
	}
	indigo_safe_free(saturation_mask);
	return result;
}

static bool autofocus_repeat(indigo_device *device) {
	int repeat_delay = AGENT_IMAGER_FOCUS_DELAY_ITEM->number.value;
	for (int repeat_count = AGENT_IMAGER_FOCUS_REPEAT_ITEM->number.value; repeat_count >= 0; repeat_count--) {
		if (autofocus(device)) {
			return true;
		} else if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			return false;
		} else if (repeat_count == 0) {
			return false;
		} else {
			indigo_send_message(device, "Repeating in %d seconds, %d attempts left", repeat_delay, repeat_count);
			for (int i = repeat_delay * 5; i >= 0; i--) {
				if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE)
					return false;
				indigo_usleep(200000);
			}
			repeat_delay *= 2;
		}
	}
	return false;
}

static void autofocus_process(indigo_device *device) {
	FILTER_DEVICE_CONTEXT->running_process = true;
	DEVICE_PRIVATE_DATA->allow_subframing = true;
	DEVICE_PRIVATE_DATA->find_stars = (AGENT_IMAGER_SELECTION_X_ITEM->number.value == 0 && AGENT_IMAGER_SELECTION_Y_ITEM->number.value == 0);
	int focuser_mode = save_switch_state(device, INDIGO_FILTER_FOCUSER_INDEX, FOCUSER_MODE_PROPERTY_NAME, FOCUSER_MODE_MANUAL_ITEM_NAME);
	int upload_mode = save_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_UPLOAD_MODE_PROPERTY_NAME, NULL);
	int image_format = save_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_IMAGE_FORMAT_PROPERTY_NAME, NULL);
	allow_abort_by_mount_agent(device, true);
	disable_solver(device);
	indigo_send_message(device, "Focusing started");
	select_subframe(device);
	DEVICE_PRIVATE_DATA->restore_initial_position = AGENT_IMAGER_FOCUS_ESTIMATOR_RMS_CONTRAST_ITEM->sw.value ? false : AGENT_IMAGER_FOCUS_FAILURE_RESTORE_ITEM->sw.value;
	if (autofocus_repeat(device)) {
		AGENT_START_PROCESS_PROPERTY->state = AGENT_IMAGER_STATS_PROPERTY->state = INDIGO_OK_STATE;
		indigo_send_message(device, "Focusing finished");
	} else {
		if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			AGENT_ABORT_PROCESS_PROPERTY->state = INDIGO_OK_STATE;
			indigo_update_property(device, AGENT_ABORT_PROCESS_PROPERTY, NULL);
			indigo_send_message(device, "Focusing aborted");
		} else {
			indigo_send_message(device, "Focusing failed");
		}
		AGENT_START_PROCESS_PROPERTY->state = INDIGO_ALERT_STATE;
	}
	allow_abort_by_mount_agent(device, false);
	restore_subframe(device);
	restore_switch_state(device, INDIGO_FILTER_FOCUSER_INDEX, FOCUSER_MODE_PROPERTY_NAME, focuser_mode);
	restore_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_UPLOAD_MODE_PROPERTY_NAME, upload_mode);
	restore_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_IMAGE_FORMAT_PROPERTY_NAME, image_format);
	AGENT_IMAGER_START_PREVIEW_ITEM->sw.value = AGENT_IMAGER_START_EXPOSURE_ITEM->sw.value = AGENT_IMAGER_START_STREAMING_ITEM->sw.value = AGENT_IMAGER_START_FOCUSING_ITEM->sw.value = AGENT_IMAGER_START_SEQUENCE_ITEM->sw.value = false;
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
	indigo_update_property(device, AGENT_START_PROCESS_PROPERTY, NULL);
	FILTER_DEVICE_CONTEXT->running_process = false;
}

static bool set_property(indigo_device *device, char *name, char *value) {
	indigo_property *device_property = NULL;
	bool wait_for_solver = false;
	bool wait_for_guider = false;
	FILTER_DEVICE_CONTEXT->property_removed = false;
	int upload_mode = -1;
	int image_format = -1;
	if (!strcasecmp(name, "object")) {
		// NO-OP, for grouping only
	} else if (!strcasecmp(name, "sleep")) {
		// sleep with 0.01s resolution
		double delay = atof(value);
		while (delay > 0 && AGENT_ABORT_PROCESS_PROPERTY->state != INDIGO_BUSY_STATE) {
			indigo_usleep(10000);
			delay -= 0.01;
		}
	} else if (!strcasecmp(name, "focus")) {
		DEVICE_PRIVATE_DATA->focus_exposure = atof(value);
	} else if (!strcasecmp(name, "count")) {
		AGENT_IMAGER_BATCH_COUNT_ITEM->number.target = atoi(value);
		indigo_update_property(device, AGENT_IMAGER_BATCH_PROPERTY, NULL);
	} else if (!strcasecmp(name, "exposure")) {
		AGENT_IMAGER_BATCH_EXPOSURE_ITEM->number.target = AGENT_IMAGER_BATCH_EXPOSURE_ITEM->number.value = atof(value);
		indigo_update_property(device, AGENT_IMAGER_BATCH_PROPERTY, NULL);
	} else if (!strcasecmp(name, "delay")) {
		AGENT_IMAGER_BATCH_DELAY_ITEM->number.target = AGENT_IMAGER_BATCH_DELAY_ITEM->number.value = atof(value);
		indigo_update_property(device, AGENT_IMAGER_BATCH_PROPERTY, NULL);
	} else if (!strcasecmp(name, "filter")) {
		AGENT_IMAGER_STATS_PHASE_ITEM->number.value = INDIGO_IMAGER_PHASE_SETTING_FILTER;
		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		if (indigo_filter_cached_property(device, INDIGO_FILTER_WHEEL_INDEX, WHEEL_SLOT_PROPERTY_NAME, &device_property, NULL)) {
			for (int j = 0; j < AGENT_WHEEL_FILTER_PROPERTY->count; j++) {
				indigo_item *item = AGENT_WHEEL_FILTER_PROPERTY->items + j;
				if (!strcasecmp(value, item->label) || !strcasecmp(value, item->name)) {
					indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, WHEEL_SLOT_ITEM_NAME, j + 1);
					break;
				}
			}
		}
	} else if (!strcasecmp(name, "mode")) {
		if (indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, CCD_MODE_PROPERTY_NAME, &device_property, NULL)) {
			for (int j = 0; j < device_property->count; j++) {
				indigo_item *item = device_property->items + j;
				if (!strcasecmp(item->label, value) || !strcasecmp(item->name, value)) {
					indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, item->name, true);
					break;
				}
			}
		}
	} else if (!strcasecmp(name, "name")) {
		if (indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, CCD_LOCAL_MODE_PROPERTY_NAME, &device_property, NULL))
			indigo_change_text_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, CCD_LOCAL_MODE_PREFIX_ITEM_NAME, value);
	} else if (!strcasecmp(name, "gain")) {
		if (indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, CCD_GAIN_PROPERTY_NAME, &device_property, NULL))
			indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, CCD_GAIN_ITEM_NAME, atof(value));
	} else if (!strcasecmp(name, "offset")) {
		if (indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, CCD_OFFSET_PROPERTY_NAME, &device_property, NULL))
			indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, CCD_OFFSET_ITEM_NAME, atof(value));
	} else if (!strcasecmp(name, "gamma")) {
		if (indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, CCD_GAMMA_PROPERTY_NAME, &device_property, NULL))
			indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, CCD_GAMMA_ITEM_NAME, atof(value));
	} else if (!strcasecmp(name, "temperature")) {
		if (indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, CCD_TEMPERATURE_PROPERTY_NAME, &device_property, NULL))
			indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, CCD_TEMPERATURE_ITEM_NAME, atof(value));
	} else if (!strcasecmp(name, "cooler")) {
		if (indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, CCD_COOLER_PROPERTY_NAME, &device_property, NULL)) {
			for (int j = 0; j < device_property->count; j++) {
				indigo_item *item = device_property->items + j;
				if (!strcasecmp(item->label, value) || !strcasecmp(item->name, value)) {
					indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, item->name, true);
					break;
				}
			}
		}
	} else if (!strcasecmp(name, "frame")) {
		if (indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, CCD_FRAME_TYPE_PROPERTY_NAME, &device_property, NULL)) {
			for (int j = 0; j < device_property->count; j++) {
				indigo_item *item = device_property->items + j;
				if (!strcasecmp(item->label, value) || !strcasecmp(item->name, value)) {
					indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, item->name, true);
					break;
				}
			}
		}
	} else if (!strcasecmp(name, "aperture")) {
		if (indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, DSLR_APERTURE_PROPERTY_NAME, &device_property, NULL)) {
			for (int j = 0; j < device_property->count; j++) {
				indigo_item *item = device_property->items + j;
				if (!strcasecmp(item->label, value) || !strcasecmp(item->name, value)) {
					indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, item->name, true);
					break;
				}
			}
		}
	} else if (!strcasecmp(name, "shutter")) {
		if (indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, DSLR_SHUTTER_PROPERTY_NAME, &device_property, NULL)) {
			for (int j = 0; j < device_property->count; j++) {
				indigo_item *item = device_property->items + j;
				if (!strcasecmp(item->label, value) || !strcasecmp(item->name, value)) {
					indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, item->name, true);
					break;
				}
			}
		}
	} else if (!strcasecmp(name, "iso")) {
		if (indigo_filter_cached_property(device, INDIGO_FILTER_CCD_INDEX, DSLR_ISO_PROPERTY_NAME, &device_property, NULL)) {
			for (int j = 0; j < device_property->count; j++) {
				indigo_item *item = device_property->items + j;
				if (!strcasecmp(item->label, value) || !strcasecmp(item->name, value)) {
					indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, item->name, true);
					break;
				}
			}
		}
// rotator is moved to mount agent
//	} else if (!strcasecmp(name, "angle")) {
//		AGENT_IMAGER_STATS_PHASE_ITEM->number.value = INDIGO_IMAGER_PHASE_ROTATING;
//		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
//		if (indigo_filter_cached_property(device, INDIGO_FILTER_ROTATOR_INDEX, ROTATOR_ON_POSITION_SET_PROPERTY_NAME, &device_property, NULL)) {
//			indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, ROTATOR_ON_POSITION_SET_GOTO_ITEM_NAME, true);
//			if (indigo_filter_cached_property(device, INDIGO_FILTER_ROTATOR_INDEX, ROTATOR_POSITION_PROPERTY_NAME, &device_property, NULL)) {
//				indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, device_property->device, device_property->name, ROTATOR_POSITION_ITEM_NAME, indigo_atod(value));
//			}
//		}
	} else if (!strcasecmp(name, "ra")) {
		DEVICE_PRIVATE_DATA->solver_goto_ra = indigo_atod(value);
	} else if (!strcasecmp(name, "dec")) {
		DEVICE_PRIVATE_DATA->solver_goto_dec = indigo_atod(value);
	} else if (!strcasecmp(name, "goto")) {
		upload_mode = save_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_UPLOAD_MODE_PROPERTY_NAME, CCD_UPLOAD_MODE_CLIENT_ITEM_NAME);
		image_format = save_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_IMAGE_FORMAT_PROPERTY_NAME, CCD_IMAGE_FORMAT_RAW_ITEM_NAME);
		AGENT_IMAGER_STATS_PHASE_ITEM->number.value = INDIGO_IMAGER_PHASE_SLEWING;
		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		if (!strcmp(value, "precise")) {
			DEVICE_PRIVATE_DATA->related_solver_process_state = INDIGO_IDLE_STATE;
			solver_precise_goto(device);
			wait_for_solver = true;
		} else if (!strcmp(value, "slew")) {
			// TODO: non-precise goto is not implemented in solver agent yet
			wait_for_solver = true;
		}
	} else if (!strcasecmp(name, "calibrate")) {
		AGENT_IMAGER_STATS_PHASE_ITEM->number.value = INDIGO_IMAGER_PHASE_CALIBRATING;
		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		DEVICE_PRIVATE_DATA->related_guider_process_state = INDIGO_IDLE_STATE;
		calibrate_guider(device, atof(value));
		wait_for_guider = true;
	} else if (!strcasecmp(name, "guide")) {
		if (!strcmp(value, "off")) {
			stop_guider(device);
		} else {
			start_guider(device, atof(value));
			wait_for_guider = true;
		}
	} else if (!strcasecmp(name, "start")) {
	} else {
		indigo_send_message(device, "Unknown sequencer command '%s'", name);
		return false;
	}
	if (device_property) {
		indigo_usleep(200000);
		while (!FILTER_DEVICE_CONTEXT->property_removed && device_property->state == INDIGO_BUSY_STATE) {
			indigo_usleep(200000);
		}
		if (device_property->state != INDIGO_OK_STATE) {
			indigo_send_message(device, "Failed to set '%s'", device_property->name);
			return false;
		}
		return true;
	} else if (wait_for_solver) {
		while (DEVICE_PRIVATE_DATA->related_solver_process_state != INDIGO_BUSY_STATE && AGENT_ABORT_PROCESS_PROPERTY->state != INDIGO_BUSY_STATE) {
			indigo_usleep(200000);
		}
		while (DEVICE_PRIVATE_DATA->related_solver_process_state == INDIGO_BUSY_STATE && AGENT_ABORT_PROCESS_PROPERTY->state != INDIGO_BUSY_STATE) {
			indigo_usleep(200000);
		}
		if (DEVICE_PRIVATE_DATA->related_solver_process_state == INDIGO_BUSY_STATE) {
			abort_solver(device);
		}
		disable_solver(device);
		restore_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_UPLOAD_MODE_PROPERTY_NAME, upload_mode);
		restore_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_IMAGE_FORMAT_PROPERTY_NAME, image_format);
		return DEVICE_PRIVATE_DATA->related_solver_process_state == INDIGO_OK_STATE;
	} else if (wait_for_guider) { // wait for guider
		DEVICE_PRIVATE_DATA->guiding = false;
		while (!DEVICE_PRIVATE_DATA->guiding && DEVICE_PRIVATE_DATA->related_guider_process_state != INDIGO_ALERT_STATE && AGENT_ABORT_PROCESS_PROPERTY->state != INDIGO_BUSY_STATE) {
			indigo_usleep(200000);
		}
		if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			stop_guider(device);
		}
		return DEVICE_PRIVATE_DATA->guiding;
	}
	return true;
}

static void sequence_process(indigo_device *device) {
	FILTER_DEVICE_CONTEXT->running_process = true;
	char *sequence_text, *sequence_text_pnt, *value;

	AGENT_IMAGER_STATS_BATCH_INDEX_ITEM->number.value =
	AGENT_IMAGER_STATS_BATCH_ITEM->number.value =
	AGENT_IMAGER_STATS_PHASE_ITEM->number.value =
	AGENT_IMAGER_STATS_BATCHES_ITEM->number.value = 0;
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);

	DEVICE_PRIVATE_DATA->focus_exposure = 0;
	DEVICE_PRIVATE_DATA->allow_subframing = false;
	DEVICE_PRIVATE_DATA->find_stars = false;
	int sequence_size = AGENT_IMAGER_SEQUENCE_PROPERTY->count - 1;
	sequence_text = indigo_safe_malloc_copy(strlen(indigo_get_text_item_value(AGENT_IMAGER_SEQUENCE_ITEM)) + 1, indigo_get_text_item_value(AGENT_IMAGER_SEQUENCE_ITEM));
	bool focuser_needed = strstr(sequence_text, "focus") != NULL;
	bool wheel_needed = strstr(sequence_text, "filter") != NULL;
	bool rotator_needed = strstr(sequence_text, "angle") != NULL;
	bool mount_needed = strstr(sequence_text, "park") != NULL;
	bool guider_needed = strstr(sequence_text, "guide") != NULL || strstr(sequence_text, "calibrate") != NULL;
	bool solver_needed = strstr(sequence_text, "precise") != NULL;
	for (char *token = strtok_r(sequence_text, ";", &sequence_text_pnt); token; token = strtok_r(NULL, ";", &sequence_text_pnt)) {
		if (strchr(token, '='))
			continue;
		if (!strcmp(token, "park"))
			continue;
		if (!strcmp(token, "unpark"))
			continue;
		int batch_index = atoi(token);
		if (batch_index < 1 || batch_index > sequence_size) {
			continue;
		}
		AGENT_IMAGER_STATS_BATCHES_ITEM->number.value++;
		if (strstr(AGENT_IMAGER_SEQUENCE_PROPERTY->items[batch_index].text.value, "focus") != NULL) {
			focuser_needed = true;
		}
		if (strstr(AGENT_IMAGER_SEQUENCE_PROPERTY->items[batch_index].text.value, "filter") != NULL) {
			wheel_needed = true;
		}
		if (strstr(AGENT_IMAGER_SEQUENCE_PROPERTY->items[batch_index].text.value, "angle") != NULL) {
			rotator_needed = true;
		}
		if (strstr(AGENT_IMAGER_SEQUENCE_PROPERTY->items[batch_index].text.value, "guide") != NULL || strstr(AGENT_IMAGER_SEQUENCE_PROPERTY->items[batch_index].text.value, "calibrate") != NULL) {
			guider_needed = true;
		}
		if (strstr(AGENT_IMAGER_SEQUENCE_PROPERTY->items[batch_index].text.value, "precise") != NULL) {
			solver_needed = true;
		}
	}
	if (focuser_needed && FILTER_FOCUSER_LIST_PROPERTY->items->sw.value) {
		AGENT_IMAGER_START_PREVIEW_ITEM->sw.value =
		AGENT_IMAGER_START_EXPOSURE_ITEM->sw.value =
		AGENT_IMAGER_START_STREAMING_ITEM->sw.value =
		AGENT_IMAGER_START_FOCUSING_ITEM->sw.value =
		AGENT_IMAGER_START_SEQUENCE_ITEM->sw.value = false;
		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		AGENT_START_PROCESS_PROPERTY->state = INDIGO_ALERT_STATE;
		FILTER_DEVICE_CONTEXT->running_process = false;
		indigo_update_property(device, AGENT_START_PROCESS_PROPERTY, "No focuser is selected");
		indigo_safe_free(sequence_text);
		return;
	}
	if (wheel_needed && FILTER_WHEEL_LIST_PROPERTY->items->sw.value) {
		AGENT_IMAGER_START_PREVIEW_ITEM->sw.value =
		AGENT_IMAGER_START_EXPOSURE_ITEM->sw.value =
		AGENT_IMAGER_START_STREAMING_ITEM->sw.value =
		AGENT_IMAGER_START_FOCUSING_ITEM->sw.value =
		AGENT_IMAGER_START_SEQUENCE_ITEM->sw.value = false;
		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		AGENT_START_PROCESS_PROPERTY->state = INDIGO_ALERT_STATE;
		FILTER_DEVICE_CONTEXT->running_process = false;
		indigo_update_property(device, AGENT_START_PROCESS_PROPERTY, "No filter wheen is selected");
		indigo_safe_free(sequence_text);
		return;
	}
// rotator is moved to mount agent
//	if (rotator_needed && FILTER_ROTATOR_LIST_PROPERTY->items->sw.value) {
//		AGENT_IMAGER_START_PREVIEW_ITEM->sw.value =
//		AGENT_IMAGER_START_EXPOSURE_ITEM->sw.value =
//		AGENT_IMAGER_START_STREAMING_ITEM->sw.value =
//		AGENT_IMAGER_START_FOCUSING_ITEM->sw.value =
//		AGENT_IMAGER_START_SEQUENCE_ITEM->sw.value = false;
//		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
//		AGENT_START_PROCESS_PROPERTY->state = INDIGO_ALERT_STATE;
//		FILTER_DEVICE_CONTEXT->running_process = false;
//		indigo_update_property(device, AGENT_START_PROCESS_PROPERTY, "No rotator is selected");
//		indigo_safe_free(sequence_text);
//		return;
//	}
	if (mount_needed && indigo_filter_first_related_agent(device, "Mount Agent") == NULL) {
		AGENT_IMAGER_START_PREVIEW_ITEM->sw.value =
		AGENT_IMAGER_START_EXPOSURE_ITEM->sw.value =
		AGENT_IMAGER_START_STREAMING_ITEM->sw.value =
		AGENT_IMAGER_START_FOCUSING_ITEM->sw.value =
		AGENT_IMAGER_START_SEQUENCE_ITEM->sw.value = false;
		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		AGENT_START_PROCESS_PROPERTY->state = INDIGO_ALERT_STATE;
		FILTER_DEVICE_CONTEXT->running_process = false;
		indigo_update_property(device, AGENT_START_PROCESS_PROPERTY, "No mount agent is selected");
		indigo_safe_free(sequence_text);
		return;
	}
	if (guider_needed && indigo_filter_first_related_agent(device, "Guider Agent") == NULL) {
		AGENT_IMAGER_START_PREVIEW_ITEM->sw.value =
		AGENT_IMAGER_START_EXPOSURE_ITEM->sw.value =
		AGENT_IMAGER_START_STREAMING_ITEM->sw.value =
		AGENT_IMAGER_START_FOCUSING_ITEM->sw.value =
		AGENT_IMAGER_START_SEQUENCE_ITEM->sw.value = false;
		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		AGENT_START_PROCESS_PROPERTY->state = INDIGO_ALERT_STATE;
		FILTER_DEVICE_CONTEXT->running_process = false;
		indigo_update_property(device, AGENT_START_PROCESS_PROPERTY, "No guider agent is selected");
		indigo_safe_free(sequence_text);
		return;
	}
	if (solver_needed && indigo_filter_first_related_agent_2(device, "Astrometry Agent", "ASTAP Agent") == NULL) {
		AGENT_IMAGER_START_PREVIEW_ITEM->sw.value =
		AGENT_IMAGER_START_EXPOSURE_ITEM->sw.value =
		AGENT_IMAGER_START_STREAMING_ITEM->sw.value =
		AGENT_IMAGER_START_FOCUSING_ITEM->sw.value =
		AGENT_IMAGER_START_SEQUENCE_ITEM->sw.value = false;
		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		AGENT_START_PROCESS_PROPERTY->state = INDIGO_ALERT_STATE;
		FILTER_DEVICE_CONTEXT->running_process = false;
		indigo_update_property(device, AGENT_START_PROCESS_PROPERTY, "No solver agent is selected");
		indigo_safe_free(sequence_text);
		return;
	}
	indigo_send_message(device, "Sequence started");
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
	strcpy(sequence_text, indigo_get_text_item_value(AGENT_IMAGER_SEQUENCE_ITEM));
	for (char *token = strtok_r(sequence_text, ";", &sequence_text_pnt); AGENT_ABORT_PROCESS_PROPERTY->state != INDIGO_BUSY_STATE && token; token = strtok_r(NULL, ";", &sequence_text_pnt)) {
		allow_abort_by_mount_agent(device, false);
		disable_solver(device);
		value = strchr(token, '=');
		if (value) {
			*value++ = 0;
			set_property(device, token, value);
			continue;
		}
		if (!strcmp(token, "park")) {
			park_mount(device);
			continue;
		}
		if (!strcmp(token, "unpark")) {
			unpark_mount(device);
			continue;
		}
		int batch_index = atoi(token);
		if (batch_index < 1 || batch_index > sequence_size) {
			continue;
		}
		indigo_send_message(device, "Batch %d started", batch_index);
		AGENT_IMAGER_STATS_FRAME_ITEM->number.value = 0;
		AGENT_IMAGER_STATS_BATCH_ITEM->number.value++;
		AGENT_IMAGER_STATS_BATCH_INDEX_ITEM->number.value = batch_index;
		AGENT_IMAGER_STATS_FRAMES_ITEM->number.value = 0;
		indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
		char batch_text[INDIGO_VALUE_SIZE], *batch_text_pnt;
		indigo_copy_value(batch_text, AGENT_IMAGER_SEQUENCE_PROPERTY->items[batch_index].text.value);
		bool valid_batch = true;
		for (char *token = strtok_r(batch_text, ";", &batch_text_pnt); token; token = strtok_r(NULL, ";", &batch_text_pnt)) {
			value = strchr(token, '=');
			if (value == NULL) {
				continue;
			}
			*value++ = 0;
			if (!set_property(device, token, value)) {
				valid_batch = false;
			}
		}
		if (valid_batch) {
			allow_abort_by_mount_agent(device, true);
			if (DEVICE_PRIVATE_DATA->focus_exposure > 0) {
				AGENT_IMAGER_STATS_PHASE_ITEM->number.value = INDIGO_IMAGER_PHASE_FOCUSING;
				indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
				int image_format = save_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_IMAGE_FORMAT_PROPERTY_NAME, NULL);
				double exposure = AGENT_IMAGER_BATCH_EXPOSURE_ITEM->number.target;
				AGENT_IMAGER_BATCH_EXPOSURE_ITEM->number.target = AGENT_IMAGER_BATCH_EXPOSURE_ITEM->number.value = DEVICE_PRIVATE_DATA->focus_exposure;
				indigo_update_property(device, AGENT_IMAGER_BATCH_PROPERTY, NULL);
				DEVICE_PRIVATE_DATA->find_stars = (AGENT_IMAGER_SELECTION_X_ITEM->number.value == 0 && AGENT_IMAGER_SELECTION_Y_ITEM->number.value == 0);
				indigo_send_message(device, "Autofocus started");
				DEVICE_PRIVATE_DATA->restore_initial_position = true;
				bool success = autofocus_repeat(device);
				if (success) {
					indigo_send_message(device, "Autofocus finished");
				} else {
					if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
						indigo_send_message(device, "Autofocus aborted");
					} else {
						indigo_send_message(device, "Autofocus failed");
					}
				}
				restore_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_IMAGE_FORMAT_PROPERTY_NAME, image_format);
				AGENT_IMAGER_BATCH_EXPOSURE_ITEM->number.target = AGENT_IMAGER_BATCH_EXPOSURE_ITEM->number.value = exposure;
				indigo_update_property(device, AGENT_IMAGER_BATCH_PROPERTY, NULL);
				DEVICE_PRIVATE_DATA->focus_exposure = 0;
				if (!success) break;
			}
			if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
				break;
			}
			if (exposure_batch(device)) {
				indigo_send_message(device, "Batch %d finished", batch_index);
			} else {
				indigo_send_message(device, "Batch %d failed", batch_index);
				continue;
			}
		} else {
			indigo_send_message(device, "Batch %d failed", batch_index);
			continue;
		}
	}
	allow_abort_by_mount_agent(device, false);
	indigo_safe_free(sequence_text);
	if (AGENT_START_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
		AGENT_START_PROCESS_PROPERTY->state = AGENT_IMAGER_STATS_PROPERTY->state = INDIGO_OK_STATE;
		// Sometimes blob arrives after the end of the sequence - gives sime time to the blob update
		indigo_usleep(0.2 * ONE_SECOND_DELAY);
		indigo_send_message(device, "Sequence finished");
	} else {
		indigo_send_message(device, "Sequence failed");
	}
	AGENT_IMAGER_STATS_PHASE_ITEM->number.value = INDIGO_IMAGER_PHASE_IDLE;
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
	AGENT_IMAGER_START_PREVIEW_ITEM->sw.value = AGENT_IMAGER_START_EXPOSURE_ITEM->sw.value = AGENT_IMAGER_START_STREAMING_ITEM->sw.value = AGENT_IMAGER_START_FOCUSING_ITEM->sw.value = AGENT_IMAGER_START_SEQUENCE_ITEM->sw.value = false;
	indigo_update_property(device, AGENT_START_PROCESS_PROPERTY, NULL);
	if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
		AGENT_ABORT_PROCESS_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, AGENT_ABORT_PROCESS_PROPERTY, NULL);
	}
	FILTER_DEVICE_CONTEXT->running_process = false;
}

static void find_stars_process(indigo_device *device) {
	FILTER_DEVICE_CONTEXT->running_process = true;
	DEVICE_PRIVATE_DATA->allow_subframing = false;
	DEVICE_PRIVATE_DATA->find_stars = true;
	int upload_mode = save_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_UPLOAD_MODE_PROPERTY_NAME, NULL);
	int image_format = save_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_IMAGE_FORMAT_PROPERTY_NAME, NULL);
	AGENT_IMAGER_STATS_FRAME_ITEM->number.value = 0;
	disable_solver(device);
	if (capture_raw_frame(device, NULL) != INDIGO_OK_STATE) {
		AGENT_IMAGER_STARS_PROPERTY->state = INDIGO_ALERT_STATE;
		indigo_update_property(device, AGENT_IMAGER_STARS_PROPERTY, NULL);
	}
	AGENT_IMAGER_START_PREVIEW_ITEM->sw.value = AGENT_IMAGER_START_EXPOSURE_ITEM->sw.value = AGENT_IMAGER_START_STREAMING_ITEM->sw.value = AGENT_IMAGER_START_FOCUSING_ITEM->sw.value = AGENT_IMAGER_START_SEQUENCE_ITEM->sw.value = false;
	AGENT_START_PROCESS_PROPERTY->state = AGENT_IMAGER_STATS_PROPERTY->state = INDIGO_OK_STATE;
	restore_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_UPLOAD_MODE_PROPERTY_NAME, upload_mode);
	restore_switch_state(device, INDIGO_FILTER_CCD_INDEX, CCD_IMAGE_FORMAT_PROPERTY_NAME, image_format);
	indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
	indigo_update_property(device, AGENT_START_PROCESS_PROPERTY, NULL);
	if (AGENT_ABORT_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
		AGENT_ABORT_PROCESS_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, AGENT_ABORT_PROCESS_PROPERTY, NULL);
	}
	FILTER_DEVICE_CONTEXT->running_process = false;
}

static void abort_process(indigo_device *device) {
	if (AGENT_IMAGER_RESUME_CONDITION_BARRIER_ITEM->sw.value) {
		// Stop process on related imager agents
		indigo_property *related_agents_property = FILTER_DEVICE_CONTEXT->filter_related_agent_list_property;
		for (int i = 0; i < related_agents_property->count; i++) {
			indigo_item *item = related_agents_property->items + i;
			if (item->sw.value && !strncmp(item->name, "Imager Agent", 12))
				indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, item->name, AGENT_ABORT_PROCESS_PROPERTY_NAME, AGENT_ABORT_PROCESS_ITEM_NAME, true);
		}
	}

	if (DEVICE_PRIVATE_DATA->use_aux_1 && FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_AUX_1_INDEX][0] != '\0') {
		indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_AUX_1_INDEX], CCD_ABORT_EXPOSURE_PROPERTY_NAME, CCD_ABORT_EXPOSURE_ITEM_NAME, true);
	}
	if (FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_CCD_INDEX][0] != '\0') {
		indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_CCD_INDEX], CCD_ABORT_EXPOSURE_PROPERTY_NAME, CCD_ABORT_EXPOSURE_ITEM_NAME, true);
	}
	if (FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_FOCUSER_INDEX][0] != '\0') {
		indigo_change_switch_property_1(FILTER_DEVICE_CONTEXT->client, FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_FOCUSER_INDEX], FOCUSER_ABORT_MOTION_PROPERTY_NAME, FOCUSER_ABORT_MOTION_ITEM_NAME, true);
	}
}

static int image_filter(const struct dirent *entry) {
	return strstr(entry->d_name, ".fits") || strstr(entry->d_name, ".xisf") || strstr(entry->d_name, ".raw") || strstr(entry->d_name, ".jpeg") || strstr(entry->d_name, ".tiff") || strstr(entry->d_name, ".avi") || strstr(entry->d_name, ".ser") || strstr(entry->d_name, ".nef") || strstr(entry->d_name, ".cr") || strstr(entry->d_name, ".sr") || strstr(entry->d_name, ".arw") || strstr(entry->d_name, ".raf");
}

static char imagedir[INDIGO_VALUE_SIZE] = "";

static inline int datetimesort(const struct dirent **a, const struct dirent **b) {
    int rc;
    struct stat stat1, stat2;
    char path1[INDIGO_VALUE_SIZE], path2[INDIGO_VALUE_SIZE];

    snprintf(path1, INDIGO_VALUE_SIZE, "%s/%s", imagedir, (*a)->d_name);
    snprintf(path2, INDIGO_VALUE_SIZE, "%s/%s", imagedir, (*b)->d_name);

    rc = stat(path1, &stat1);
    if (rc) {
        INDIGO_DRIVER_ERROR(DRIVER_NAME, "Can not stat %s", path1);
        return 0;
    }
    rc = stat(path2, &stat2);
    if (rc) {
        INDIGO_DRIVER_ERROR(DRIVER_NAME, "Can not stat %s", path1);
        return 0;
    }

	if (stat1.st_mtime > stat2.st_mtime) return 1;
	if (stat1.st_mtime < stat2.st_mtime) return -1;
	if (stat1.st_mtime == stat2.st_mtime) {
		#if defined(INDIGO_LINUX)
		if (stat1.st_mtim.tv_nsec > stat2.st_mtim.tv_nsec) return 1;
		if (stat1.st_mtim.tv_nsec < stat2.st_mtim.tv_nsec) return -1;
		#elif defined(INDIGO_MACOS)
		if (stat1.st_mtimespec.tv_nsec > stat2.st_mtimespec.tv_nsec) return 1;
		if (stat1.st_mtimespec.tv_nsec < stat2.st_mtimespec.tv_nsec) return -1;
		#endif
	}
	return 0;
}

static void setup_download(indigo_device *device) {
	if (*DEVICE_PRIVATE_DATA->current_folder) {
		indigo_delete_property(device, AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY, NULL);
		struct dirent **entries;
		strncpy(imagedir, DEVICE_PRIVATE_DATA->current_folder, INDIGO_VALUE_SIZE);
		int count = scandir(DEVICE_PRIVATE_DATA->current_folder, &entries, image_filter, datetimesort);
		if (count >= 0) {
			AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY = indigo_resize_property(AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY, count + 1);
			char file_name[INDIGO_VALUE_SIZE + INDIGO_NAME_SIZE];
			struct stat file_stat;
			int valid_count = 1; /* Refresh item is 0 */
			for (int i = 0; i < count; i++) {
				strcpy(file_name, DEVICE_PRIVATE_DATA->current_folder);
				strcat(file_name, entries[i]->d_name);
				if (stat(file_name, &file_stat) >= 0 && file_stat.st_size > 0) {
					indigo_init_switch_item(AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY->items + valid_count++, entries[i]->d_name, entries[i]->d_name, false);
				}
				free(entries[i]);
			}
			AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY->count = valid_count;
			free(entries);
		}
		AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY->state = INDIGO_OK_STATE;
		indigo_define_property(device, AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY, NULL);
	}
}

// -------------------------------------------------------------------------------- INDIGO agent device implementation

static indigo_result agent_enumerate_properties(indigo_device *device, indigo_client *client, indigo_property *property);

static bool validate_device(indigo_device *device, int index, indigo_property *info_property, int mask) {
	if (index == INDIGO_FILTER_AUX_1_INDEX && mask != INDIGO_INTERFACE_AUX_SHUTTER)
		return false;
	return true;
}

static indigo_result agent_device_attach(indigo_device *device) {
	assert(device != NULL);
	assert(DEVICE_PRIVATE_DATA != NULL);
	if (indigo_filter_device_attach(device, DRIVER_NAME, DRIVER_VERSION, INDIGO_INTERFACE_CCD | INDIGO_INTERFACE_WHEEL | INDIGO_INTERFACE_FOCUSER) == INDIGO_OK) {
		// -------------------------------------------------------------------------------- Device properties
		FILTER_CCD_LIST_PROPERTY->hidden = false;
		FILTER_WHEEL_LIST_PROPERTY->hidden = false;
		FILTER_FOCUSER_LIST_PROPERTY->hidden = false;
		FILTER_RELATED_AGENT_LIST_PROPERTY->hidden = false;
		FILTER_AUX_1_LIST_PROPERTY->hidden = false;
		strcpy(FILTER_AUX_1_LIST_PROPERTY->label, "External shutter list");
		strcpy(FILTER_AUX_1_LIST_PROPERTY->items->label, "No external shutter");
		FILTER_DEVICE_CONTEXT->validate_device = validate_device;
		// -------------------------------------------------------------------------------- Batch properties
		AGENT_IMAGER_BATCH_PROPERTY = indigo_init_number_property(NULL, device->name, AGENT_IMAGER_BATCH_PROPERTY_NAME, "Agent", "Batch settings", INDIGO_OK_STATE, INDIGO_RW_PERM, 5);
		if (AGENT_IMAGER_BATCH_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_number_item(AGENT_IMAGER_BATCH_COUNT_ITEM, AGENT_IMAGER_BATCH_COUNT_ITEM_NAME, "Frame count", -1, 0xFFFF, 1, 1);
		indigo_init_number_item(AGENT_IMAGER_BATCH_EXPOSURE_ITEM, AGENT_IMAGER_BATCH_EXPOSURE_ITEM_NAME, "Exposure time (s)", 0, 0xFFFF, 1, 1);
		indigo_init_number_item(AGENT_IMAGER_BATCH_DELAY_ITEM, AGENT_IMAGER_BATCH_DELAY_ITEM_NAME, "Delay after each exposure (s)", 0, 0xFFFF, 1, 0);
		indigo_init_number_item(AGENT_IMAGER_BATCH_FRAMES_TO_SKIP_BEFORE_DITHER_ITEM, AGENT_IMAGER_BATCH_FRAMES_TO_SKIP_BEFORE_DITHER_ITEM_NAME, "Frames to skip before dither", -1, 1000, 1, 0);
		indigo_init_number_item(AGENT_IMAGER_BATCH_PAUSE_AFTER_TRANSIT_ITEM, AGENT_IMAGER_BATCH_PAUSE_AFTER_TRANSIT_ITEM_NAME, "Pause after transit (h)", -2, 2, 1, 0);
		strcpy(AGENT_IMAGER_BATCH_PAUSE_AFTER_TRANSIT_ITEM->number.format, "%12.3m");
		// -------------------------------------------------------------------------------- Focus properties
		AGENT_IMAGER_FOCUS_PROPERTY = indigo_init_number_property(NULL, device->name, AGENT_IMAGER_FOCUS_PROPERTY_NAME, "Agent", "Autofocus settings", INDIGO_OK_STATE, INDIGO_RW_PERM, 10);
		if (AGENT_IMAGER_FOCUS_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_number_item(AGENT_IMAGER_FOCUS_INITIAL_ITEM, AGENT_IMAGER_FOCUS_INITIAL_ITEM_NAME, "Initial / U-Curve step", 1, 0xFFFF, 1, 20);
		indigo_init_number_item(AGENT_IMAGER_FOCUS_FINAL_ITEM, AGENT_IMAGER_FOCUS_FINAL_ITEM_NAME, "Final step", 1, 0xFFFF, 1, 5);
		indigo_init_number_item(AGENT_IMAGER_FOCUS_UCURVE_SAMPLES_ITEM, AGENT_IMAGER_FOCUS_UCURVE_SAMPLES_ITEM_NAME, "U-Curve fitting samples", 6, MAX_UCURVE_SAMPLES, 1, 10);
		indigo_init_number_item(AGENT_IMAGER_FOCUS_BACKLASH_ITEM, AGENT_IMAGER_FOCUS_BACKLASH_ITEM_NAME, "Backlash (both)", 0, 0xFFFF, 1, 0);
		indigo_init_number_item(AGENT_IMAGER_FOCUS_BACKLASH_IN_ITEM, AGENT_IMAGER_FOCUS_BACKLASH_IN_ITEM_NAME, "Backlash (in)", 0, 0xFFFF, 1, 0);
		indigo_init_number_item(AGENT_IMAGER_FOCUS_BACKLASH_OUT_ITEM, AGENT_IMAGER_FOCUS_BACKLASH_OUT_ITEM_NAME, "Backlash (out)", 0, 0xFFFF, 1, 0);
		indigo_init_number_item(AGENT_IMAGER_FOCUS_BACKLASH_OVERSHOOT_ITEM, AGENT_IMAGER_FOCUS_BACKLASH_OVERSHOOT_ITEM_NAME, "Backlash overshoot factor (1 disabled)", 1, 3, 0.5, 1);
		indigo_init_number_item(AGENT_IMAGER_FOCUS_STACK_ITEM, AGENT_IMAGER_FOCUS_STACK_ITEM_NAME, "Stacking", 1, 5, 1, 3);
		indigo_init_number_item(AGENT_IMAGER_FOCUS_REPEAT_ITEM, AGENT_IMAGER_FOCUS_REPEAT_ITEM_NAME, "Repeat count", 0, 10, 1, 0);
		indigo_init_number_item(AGENT_IMAGER_FOCUS_DELAY_ITEM, AGENT_IMAGER_FOCUS_DELAY_ITEM_NAME, "Initial repeat delay (s)", 0, 3600, 1, 0);
		// -------------------------------------------------------------------------------- Focus failure handling
		AGENT_IMAGER_FOCUS_FAILURE_PROPERTY = indigo_init_switch_property(NULL, device->name, AGENT_IMAGER_FOCUS_FAILURE_PROPERTY_NAME, "Agent", "On Peak / HFD autofocus failure", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ONE_OF_MANY_RULE, 2);
		if (AGENT_IMAGER_FOCUS_FAILURE_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_switch_item(AGENT_IMAGER_FOCUS_FAILURE_STOP_ITEM, AGENT_IMAGER_FOCUS_FAILURE_STOP_ITEM_NAME, "Stop on failure", false);
		indigo_init_switch_item(AGENT_IMAGER_FOCUS_FAILURE_RESTORE_ITEM, AGENT_IMAGER_FOCUS_FAILURE_RESTORE_ITEM_NAME, "Goto starting position", true);
		// -------------------------------------------------------------------------------- Focus Quality Estimator
		AGENT_IMAGER_FOCUS_ESTIMATOR_PROPERTY = indigo_init_switch_property(NULL, device->name, AGENT_IMAGER_FOCUS_ESTIMATOR_PROPERTY_NAME, "Agent", "Autofocus estimator", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ONE_OF_MANY_RULE, 3);
		if (AGENT_IMAGER_FOCUS_ESTIMATOR_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_switch_item(AGENT_IMAGER_FOCUS_ESTIMATOR_UCURVE_ITEM, AGENT_IMAGER_FOCUS_ESTIMATOR_UCURVE_ITEM_NAME, "U-Curve", true);
		indigo_init_switch_item(AGENT_IMAGER_FOCUS_ESTIMATOR_HFD_PEAK_ITEM, AGENT_IMAGER_FOCUS_ESTIMATOR_HFD_PEAK_ITEM_NAME, "Peak / HFD", false);
		indigo_init_switch_item(AGENT_IMAGER_FOCUS_ESTIMATOR_RMS_CONTRAST_ITEM, AGENT_IMAGER_FOCUS_ESTIMATOR_RMS_CONTRAST_ITEM_NAME, "RMS contrast", false);
		// -------------------------------------------------------------------------------- Process properties
		AGENT_START_PROCESS_PROPERTY = indigo_init_switch_property(NULL, device->name, AGENT_START_PROCESS_PROPERTY_NAME, "Agent", "Start process", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_AT_MOST_ONE_RULE, 5);
		if (AGENT_START_PROCESS_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_switch_item(AGENT_IMAGER_START_PREVIEW_ITEM, AGENT_IMAGER_START_PREVIEW_ITEM_NAME, "Start preview", false);
		indigo_init_switch_item(AGENT_IMAGER_START_EXPOSURE_ITEM, AGENT_IMAGER_START_EXPOSURE_ITEM_NAME, "Start exposure batch", false);
		indigo_init_switch_item(AGENT_IMAGER_START_STREAMING_ITEM, AGENT_IMAGER_START_STREAMING_ITEM_NAME, "Start streaming batch", false);
		indigo_init_switch_item(AGENT_IMAGER_START_FOCUSING_ITEM, AGENT_IMAGER_START_FOCUSING_ITEM_NAME, "Start focusing", false);
		indigo_init_switch_item(AGENT_IMAGER_START_SEQUENCE_ITEM, AGENT_IMAGER_START_SEQUENCE_ITEM_NAME, "Start sequence", false);
		AGENT_PAUSE_PROCESS_PROPERTY = indigo_init_switch_property(NULL, device->name, AGENT_PAUSE_PROCESS_PROPERTY_NAME, "Agent", "Pause/Resume process", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_AT_MOST_ONE_RULE, 3);
		if (AGENT_PAUSE_PROCESS_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_switch_item(AGENT_PAUSE_PROCESS_ITEM, AGENT_PAUSE_PROCESS_ITEM_NAME, "Pause/resume process (with abort)", false);
		indigo_init_switch_item(AGENT_PAUSE_PROCESS_WAIT_ITEM, AGENT_PAUSE_PROCESS_WAIT_ITEM_NAME, "Pause/resume process (with wait)", false);
		indigo_init_switch_item(AGENT_PAUSE_PROCESS_AFTER_TRANSIT_ITEM, AGENT_PAUSE_PROCESS_AFTER_TRANSIT_ITEM_NAME, "Pause/resume process (at transit)", false);
		AGENT_ABORT_PROCESS_PROPERTY = indigo_init_switch_property(NULL, device->name, AGENT_ABORT_PROCESS_PROPERTY_NAME, "Agent", "Abort process", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_AT_MOST_ONE_RULE, 1);
		if (AGENT_ABORT_PROCESS_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_switch_item(AGENT_ABORT_PROCESS_ITEM, AGENT_ABORT_PROCESS_ITEM_NAME, "Abort process", false);
		AGENT_PROCESS_FEATURES_PROPERTY = indigo_init_switch_property(NULL, device->name, AGENT_PROCESS_FEATURES_PROPERTY_NAME, "Agent", "Process features", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ANY_OF_MANY_RULE, 3);
		if (AGENT_PROCESS_FEATURES_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_switch_item(AGENT_IMAGER_ENABLE_DITHERING_FEATURE_ITEM, AGENT_IMAGER_ENABLE_DITHERING_FEATURE_ITEM_NAME, "Enable dithering", true);
		indigo_init_switch_item(AGENT_IMAGER_DITHER_AFTER_BATCH_FEATURE_ITEM, AGENT_IMAGER_DITHER_AFTER_BATCH_FEATURE_ITEM_NAME, "Dither after last frame", false);
		indigo_init_switch_item(AGENT_IMAGER_PAUSE_AFTER_TRANSIT_FEATURE_ITEM, AGENT_IMAGER_PAUSE_AFTER_TRANSIT_FEATURE_ITEM_NAME, "Pause after transit", false);
		// -------------------------------------------------------------------------------- Download properties
		AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY = indigo_init_text_property(NULL, device->name, AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY_NAME, "Agent", "Download image", INDIGO_OK_STATE, INDIGO_RW_PERM, 1);
		if (AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_text_item(AGENT_IMAGER_DOWNLOAD_FILE_ITEM, AGENT_IMAGER_DOWNLOAD_FILE_ITEM_NAME, "File name", "");
		AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY = indigo_init_switch_property(NULL, device->name, AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY_NAME, "Agent", "Download image list", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ANY_OF_MANY_RULE, INDIGO_PREALLOCATED_COUNT);
		AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY->count = 1;
		if (AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_switch_item(AGENT_IMAGER_DOWNLOAD_FILES_REFRESH_ITEM, AGENT_IMAGER_DOWNLOAD_FILES_REFRESH_ITEM_NAME, "Refresh", false);
		AGENT_IMAGER_DOWNLOAD_IMAGE_PROPERTY = indigo_init_blob_property(NULL, device->name, AGENT_IMAGER_DOWNLOAD_IMAGE_PROPERTY_NAME, "Agent", "Download image data", INDIGO_OK_STATE, 1);
		if (AGENT_IMAGER_DOWNLOAD_IMAGE_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_blob_item(AGENT_IMAGER_DOWNLOAD_IMAGE_ITEM, AGENT_IMAGER_DOWNLOAD_IMAGE_ITEM_NAME, "Image");
		AGENT_IMAGER_DELETE_FILE_PROPERTY = indigo_init_text_property(NULL, device->name, AGENT_IMAGER_DELETE_FILE_PROPERTY_NAME, "Agent", "Delete image", INDIGO_OK_STATE, INDIGO_RW_PERM, 1);
		if (AGENT_IMAGER_DELETE_FILE_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_text_item(AGENT_IMAGER_DELETE_FILE_ITEM, AGENT_IMAGER_DELETE_FILE_ITEM_NAME, "File name", "");
		// -------------------------------------------------------------------------------- Wheel helpers
		AGENT_WHEEL_FILTER_PROPERTY = indigo_init_switch_property(NULL, device->name, AGENT_WHEEL_FILTER_PROPERTY_NAME, "Agent", "Selected filter", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ONE_OF_MANY_RULE, FILTER_SLOT_COUNT);
		if (AGENT_WHEEL_FILTER_PROPERTY == NULL)
			return INDIGO_FAILED;
		for (int i = 0; i < FILTER_SLOT_COUNT; i++) {
			char name[8], label[32];
			sprintf(name, "%d", i + 1);
			sprintf(label, "Filter #%d", i + 1);
			indigo_init_switch_item(AGENT_WHEEL_FILTER_PROPERTY->items + i, name, label, false);
		}
		AGENT_WHEEL_FILTER_PROPERTY->count = 0;
		AGENT_WHEEL_FILTER_PROPERTY->hidden = true;
		// -------------------------------------------------------------------------------- Detected stars
		AGENT_IMAGER_STARS_PROPERTY = indigo_init_switch_property(NULL, device->name, AGENT_IMAGER_STARS_PROPERTY_NAME, "Agent", "Stars", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ONE_OF_MANY_RULE, MAX_STAR_COUNT + 1);
		if (AGENT_IMAGER_STARS_PROPERTY == NULL)
			return INDIGO_FAILED;
		AGENT_IMAGER_STARS_PROPERTY->count = 1;
		indigo_init_switch_item(AGENT_IMAGER_STARS_REFRESH_ITEM, AGENT_IMAGER_STARS_REFRESH_ITEM_NAME, "Refresh", false);
		// -------------------------------------------------------------------------------- Selected star
		AGENT_IMAGER_SELECTION_PROPERTY = indigo_init_number_property(NULL, device->name, AGENT_IMAGER_SELECTION_PROPERTY_NAME, "Agent", "Selection", INDIGO_OK_STATE, INDIGO_RW_PERM, 3 + 2 * INDIGO_MAX_MULTISTAR_COUNT);
		if (AGENT_IMAGER_SELECTION_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_number_item(AGENT_IMAGER_SELECTION_RADIUS_ITEM, AGENT_IMAGER_SELECTION_RADIUS_ITEM_NAME, "Radius (px)", 1, 75, 1, 12);
		indigo_init_number_item(AGENT_IMAGER_SELECTION_SUBFRAME_ITEM, AGENT_IMAGER_SELECTION_SUBFRAME_ITEM_NAME, "Subframe", 0, 10, 1, 0);
		indigo_init_number_item(AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM, AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM_NAME, "Maximum number of stars", 1, INDIGO_MAX_MULTISTAR_COUNT, 1, 1);
		for (int i = 0; i < INDIGO_MAX_MULTISTAR_COUNT; i++) {
			indigo_item *item_x = AGENT_IMAGER_SELECTION_X_ITEM + 2 * i;
			indigo_item *item_y = AGENT_IMAGER_SELECTION_Y_ITEM + 2 * i;
			char name[INDIGO_NAME_SIZE], label[INDIGO_VALUE_SIZE];
			sprintf(name, i ? "%s_%d" : "%s", AGENT_IMAGER_SELECTION_X_ITEM_NAME, i + 1);
			sprintf(label, i ? "Selection #%d X (px)" : "Selection X (px)", i + 1);
			indigo_init_number_item(item_x, name, label, 0, 0xFFFF, 1, 0);
			sprintf(name, i ? "%s_%d" : "%s", AGENT_IMAGER_SELECTION_Y_ITEM_NAME, i + 1);
			sprintf(label, i ? "Selection #%d Y (px)" : "Selection Y (px)", i + 1);
			indigo_init_number_item(item_y, name, label, 0, 0xFFFF, 1, 0);
		}
		AGENT_IMAGER_SELECTION_PROPERTY->count = 5;

		// -------------------------------------------------------------------------------- Focusing stats
		AGENT_IMAGER_STATS_PROPERTY = indigo_init_number_property(NULL, device->name, AGENT_IMAGER_STATS_PROPERTY_NAME, "Agent", "Statistics", INDIGO_OK_STATE, INDIGO_RO_PERM, 18);
		if (AGENT_IMAGER_STATS_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_number_item(AGENT_IMAGER_STATS_EXPOSURE_ITEM, AGENT_IMAGER_STATS_EXPOSURE_ITEM_NAME, "Exposure remaining (s)", 0, 3600, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_DELAY_ITEM, AGENT_IMAGER_STATS_DELAY_ITEM_NAME, "Delay remaining (s)", 0, 3600, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_FRAME_ITEM, AGENT_IMAGER_STATS_FRAME_ITEM_NAME, "Current frame", 0, 0xFFFFFFFF, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_FRAMES_ITEM, AGENT_IMAGER_STATS_FRAMES_ITEM_NAME, "Frame count", 0, 0xFFFFFFFF, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_BATCH_INDEX_ITEM, AGENT_IMAGER_STATS_BATCH_INDEX_ITEM_NAME, "Current batch index", 0, 0xFFFFFFFF, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_BATCH_ITEM, AGENT_IMAGER_STATS_BATCH_ITEM_NAME, "Current batch", 0, 0xFFFFFFFF, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_BATCHES_ITEM, AGENT_IMAGER_STATS_BATCHES_ITEM_NAME, "Batch count", 0, 0xFFFFFFFF, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_PHASE_ITEM, AGENT_IMAGER_STATS_PHASE_ITEM_NAME, "Batch phase", 0, 0xFFFFFFFF, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_DRIFT_X_ITEM, AGENT_IMAGER_STATS_DRIFT_X_ITEM_NAME, "Drift X", -1000, 1000, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_DRIFT_Y_ITEM, AGENT_IMAGER_STATS_DRIFT_Y_ITEM_NAME, "Drift Y", -1000, 1000, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_FWHM_ITEM, AGENT_IMAGER_STATS_FWHM_ITEM_NAME, "FWHM", 0, 0xFFFF, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_HFD_ITEM, AGENT_IMAGER_STATS_HFD_ITEM_NAME, "HFD", 0, 0xFFFF, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_PEAK_ITEM, AGENT_IMAGER_STATS_PEAK_ITEM_NAME, "Peak", 0, 0xFFFF, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_DITHERING_ITEM, AGENT_IMAGER_STATS_DITHERING_ITEM_NAME, "Dithering RMSE", 0, 0xFFFF, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_FOCUS_OFFSET_ITEM, AGENT_IMAGER_STATS_FOCUS_OFFSET_ITEM_NAME, "Autofocus offset", -0xFFFF, 0xFFFF, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM, AGENT_IMAGER_STATS_RMS_CONTRAST_ITEM_NAME, "RMS contrast", 0, 1, 0, 0);
		indigo_init_number_item(AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM, AGENT_IMAGER_STATS_FOCUS_DEVIATION_ITEM_NAME, "Best focus deviation (%)", -100, 100, 0, 100);
		indigo_init_number_item(AGENT_IMAGER_STATS_FRAMES_TO_DITHERING_ITEM, AGENT_IMAGER_STATS_FRAMES_TO_DITHERING_ITEM_NAME, "Frames to dithering", 0, 0xFFFFFFFF, 0, 0);
		// -------------------------------------------------------------------------------- Sequence size
		AGENT_IMAGER_SEQUENCE_SIZE_PROPERTY = indigo_init_number_property(NULL, device->name, AGENT_IMAGER_SEQUENCE_SIZE_PROPERTY_NAME, "Agent", "Sequence size", INDIGO_OK_STATE, INDIGO_RW_PERM, 1);
		if (AGENT_IMAGER_SEQUENCE_SIZE_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_number_item(AGENT_IMAGER_SEQUENCE_SIZE_ITEM, AGENT_IMAGER_SEQUENCE_SIZE_ITEM_NAME, "Number of batches", 1, MAX_SEQUENCE_SIZE, 1, SEQUENCE_SIZE);
		// -------------------------------------------------------------------------------- Sequencer
		AGENT_IMAGER_SEQUENCE_PROPERTY = indigo_init_text_property(NULL, device->name, AGENT_IMAGER_SEQUENCE_PROPERTY_NAME, "Agent", "Sequence", INDIGO_OK_STATE, INDIGO_RW_PERM, 1 + MAX_SEQUENCE_SIZE);
		if (AGENT_IMAGER_SEQUENCE_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_text_item(AGENT_IMAGER_SEQUENCE_ITEM, AGENT_IMAGER_SEQUENCE_ITEM_NAME, "Sequence", "");
		for (int i = 1; i <= MAX_SEQUENCE_SIZE; i++) {
			char name[32], label[32];
			sprintf(name, "%02d", i);
			sprintf(label, "Batch #%d", i);
			indigo_init_text_item(AGENT_IMAGER_SEQUENCE_PROPERTY->items + i, name, label, "");
		}
		AGENT_IMAGER_SEQUENCE_PROPERTY->count = SEQUENCE_SIZE + 1;
		// -------------------------------------------------------------------------------- Breakpoint support
		AGENT_IMAGER_BREAKPOINT_PROPERTY = indigo_init_switch_property(NULL, device->name, AGENT_IMAGER_BREAKPOINT_PROPERTY_NAME, MAIN_GROUP, "Breakpoints", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ANY_OF_MANY_RULE, 6);
		if (AGENT_IMAGER_BREAKPOINT_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_switch_item(AGENT_IMAGER_BREAKPOINT_PRE_BATCH_ITEM, AGENT_IMAGER_BREAKPOINT_PRE_BATCH_ITEM_NAME, "Pre-batch", false);
		indigo_init_switch_item(AGENT_IMAGER_BREAKPOINT_PRE_CAPTURE_ITEM, AGENT_IMAGER_BREAKPOINT_PRE_CAPTURE_ITEM_NAME, "Pre-capture", false);
		indigo_init_switch_item(AGENT_IMAGER_BREAKPOINT_POST_CAPTURE_ITEM, AGENT_IMAGER_BREAKPOINT_POST_CAPTURE_ITEM_NAME, "Post-capture", false);
		indigo_init_switch_item(AGENT_IMAGER_BREAKPOINT_PRE_DELAY_ITEM, AGENT_IMAGER_BREAKPOINT_PRE_DELAY_ITEM_NAME, "Pre-delay", false);
		indigo_init_switch_item(AGENT_IMAGER_BREAKPOINT_POST_DELAY_ITEM, AGENT_IMAGER_BREAKPOINT_POST_DELAY_ITEM_NAME, "Post-delay", false);
		indigo_init_switch_item(AGENT_IMAGER_BREAKPOINT_POST_BATCH_ITEM, AGENT_IMAGER_BREAKPOINT_POST_BATCH_ITEM_NAME, "Post-batch", false);
		AGENT_IMAGER_RESUME_CONDITION_PROPERTY = indigo_init_switch_property(NULL, device->name, AGENT_IMAGER_RESUME_CONDITION_PROPERTY_NAME, MAIN_GROUP, "Breakpoint resume condition", INDIGO_OK_STATE, INDIGO_RW_PERM, INDIGO_ONE_OF_MANY_RULE, 2);
		if (AGENT_IMAGER_RESUME_CONDITION_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_switch_item(AGENT_IMAGER_RESUME_CONDITION_TRIGGER_ITEM, AGENT_IMAGER_RESUME_CONDITION_TRIGGER_ITEM_NAME, "Trigger/manual", true);
		indigo_init_switch_item(AGENT_IMAGER_RESUME_CONDITION_BARRIER_ITEM, AGENT_IMAGER_RESUME_CONDITION_BARRIER_ITEM_NAME, "Barrier", false);
		AGENT_IMAGER_BARRIER_STATE_PROPERTY = indigo_init_light_property(NULL, device->name, AGENT_IMAGER_BARRIER_STATE_PROPERTY_NAME, MAIN_GROUP, "Breakpoint barrier state", INDIGO_OK_STATE, 0);
		if (AGENT_IMAGER_BARRIER_STATE_PROPERTY == NULL)
			return INDIGO_FAILED;
		// --------------------------------------------------------------------------------
		DEVICE_PRIVATE_DATA->use_hfd_estimator = true;
		DEVICE_PRIVATE_DATA->use_rms_estimator = false;
		DEVICE_PRIVATE_DATA->bin_x = DEVICE_PRIVATE_DATA->bin_y = 1;
		CONNECTION_PROPERTY->hidden = true;
		ADDITIONAL_INSTANCES_PROPERTY->hidden = DEVICE_CONTEXT->base_device != NULL;
		pthread_mutex_init(&DEVICE_PRIVATE_DATA->mutex, NULL);
		indigo_load_properties(device, false);
		INDIGO_DEVICE_ATTACH_LOG(DRIVER_NAME, device->name);
		return agent_enumerate_properties(device, NULL, NULL);
	}
	return INDIGO_FAILED;
}

static indigo_result agent_enumerate_properties(indigo_device *device, indigo_client *client, indigo_property *property) {
	if (client != NULL && client == FILTER_DEVICE_CONTEXT->client)
		return INDIGO_OK;
	if (indigo_property_match(AGENT_IMAGER_BATCH_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_BATCH_PROPERTY, NULL);
	if (indigo_property_match(AGENT_IMAGER_FOCUS_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_FOCUS_PROPERTY, NULL);
	if (indigo_property_match(AGENT_IMAGER_FOCUS_FAILURE_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_FOCUS_FAILURE_PROPERTY, NULL);
	if (indigo_property_match(AGENT_IMAGER_FOCUS_ESTIMATOR_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_FOCUS_ESTIMATOR_PROPERTY, NULL);
	if (indigo_property_match(AGENT_IMAGER_DOWNLOAD_IMAGE_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_DOWNLOAD_IMAGE_PROPERTY, NULL);
	if (indigo_property_match(AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY, NULL);
	if (indigo_property_match(AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY, NULL);
	if (indigo_property_match(AGENT_IMAGER_DELETE_FILE_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_DELETE_FILE_PROPERTY, NULL);
	if (indigo_property_match(AGENT_START_PROCESS_PROPERTY, property))
		indigo_define_property(device, AGENT_START_PROCESS_PROPERTY, NULL);
	if (indigo_property_match(AGENT_PAUSE_PROCESS_PROPERTY, property))
		indigo_define_property(device, AGENT_PAUSE_PROCESS_PROPERTY, NULL);
	if (indigo_property_match(AGENT_ABORT_PROCESS_PROPERTY, property))
		indigo_define_property(device, AGENT_ABORT_PROCESS_PROPERTY, NULL);
	if (indigo_property_match(AGENT_PROCESS_FEATURES_PROPERTY, property))
		indigo_define_property(device, AGENT_PROCESS_FEATURES_PROPERTY, NULL);
	if (indigo_property_match(AGENT_IMAGER_STARS_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_STARS_PROPERTY, NULL);
	if (indigo_property_match(AGENT_IMAGER_SELECTION_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_SELECTION_PROPERTY, NULL);
	if (indigo_property_match(AGENT_IMAGER_STATS_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
	if (indigo_property_match(AGENT_IMAGER_SEQUENCE_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_SEQUENCE_PROPERTY, NULL);
	if (indigo_property_match(AGENT_IMAGER_SEQUENCE_SIZE_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_SEQUENCE_SIZE_PROPERTY, NULL);
	if (indigo_property_match(AGENT_IMAGER_BREAKPOINT_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_BREAKPOINT_PROPERTY, NULL);
	if (indigo_property_match(AGENT_IMAGER_RESUME_CONDITION_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_RESUME_CONDITION_PROPERTY, NULL);
	if (indigo_property_match(AGENT_IMAGER_BARRIER_STATE_PROPERTY, property))
		indigo_define_property(device, AGENT_IMAGER_BARRIER_STATE_PROPERTY, NULL);
	return indigo_filter_enumerate_properties(device, client, property);
}

static indigo_result agent_change_property(indigo_device *device, indigo_client *client, indigo_property *property) {
	assert(device != NULL);
	assert(DEVICE_CONTEXT != NULL);
	assert(property != NULL);
	if (client == FILTER_DEVICE_CONTEXT->client)
		return INDIGO_OK;
	if (indigo_property_match(FILTER_CCD_LIST_PROPERTY, property)) {
// -------------------------------------------------------------------------------- FILTER_CCD_LIST_PROPERTY
		if (!FILTER_DEVICE_CONTEXT->running_process) {
			bool reset_selection = true;
			for (int i = 0; i < property->count; i++) {
				if (property->items[i].sw.value) {
					for (int j = 0; j < FILTER_CCD_LIST_PROPERTY->count; j++) {
						if (FILTER_CCD_LIST_PROPERTY->items[j].sw.value) {
							if (!strcmp(property->items[i].name, FILTER_CCD_LIST_PROPERTY->items[j].name))
								reset_selection = false;
							break;
						}
					}
				}
			}
			if (reset_selection) {
				for (int i = 0; i < AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM->number.value; i++) {
					indigo_item *item_x = AGENT_IMAGER_SELECTION_X_ITEM + 2 * i;
					indigo_item *item_y = AGENT_IMAGER_SELECTION_Y_ITEM + 2 * i;
					item_x->number.target = item_x->number.value = 0;
					item_y->number.target = item_y->number.value = 0;
				}
				indigo_update_property(device, AGENT_IMAGER_SELECTION_PROPERTY, NULL);
			}
		}
	} else if (indigo_property_match(AGENT_IMAGER_BATCH_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- AGENT_IMAGER_BATCH
		indigo_property_copy_values(AGENT_IMAGER_BATCH_PROPERTY, property, false);
		AGENT_IMAGER_BATCH_PROPERTY->state = INDIGO_OK_STATE;
		save_config(device);
		indigo_update_property(device, AGENT_IMAGER_BATCH_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(AGENT_IMAGER_FOCUS_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- AGENT_IMAGER_FOCUS
		indigo_property_copy_values(AGENT_IMAGER_FOCUS_PROPERTY, property, false);
		char *focuser_name = FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_FOCUSER_INDEX];
		if (*focuser_name) {
			indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, focuser_name, FOCUSER_BACKLASH_PROPERTY_NAME, FOCUSER_BACKLASH_ITEM_NAME, AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.value);
		}
		AGENT_IMAGER_FOCUS_PROPERTY->state = INDIGO_OK_STATE;
		save_config(device);
		indigo_update_property(device, AGENT_IMAGER_FOCUS_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(AGENT_IMAGER_FOCUS_FAILURE_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- AGENT_IMAGER_FOCUS_FAILURE
		indigo_property_copy_values(AGENT_IMAGER_FOCUS_FAILURE_PROPERTY, property, false);
		AGENT_IMAGER_FOCUS_FAILURE_PROPERTY->state = INDIGO_OK_STATE;
		save_config(device);
		indigo_update_property(device, AGENT_IMAGER_FOCUS_FAILURE_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(AGENT_IMAGER_FOCUS_ESTIMATOR_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- AGENT_IMAGER_FOCUS_ESTIMATOR
		indigo_property_copy_values(AGENT_IMAGER_FOCUS_ESTIMATOR_PROPERTY, property, false);
		AGENT_IMAGER_FOCUS_ESTIMATOR_PROPERTY->state = INDIGO_OK_STATE;
		save_config(device);
		indigo_update_property(device, AGENT_IMAGER_FOCUS_ESTIMATOR_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(AGENT_IMAGER_STARS_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- AGENT_IMAGER_STARS
		if (AGENT_START_PROCESS_PROPERTY->state != INDIGO_BUSY_STATE && AGENT_IMAGER_STARS_PROPERTY->state != INDIGO_BUSY_STATE) {
			indigo_property_copy_values(AGENT_IMAGER_STARS_PROPERTY, property, false);
			if (AGENT_IMAGER_STARS_REFRESH_ITEM->sw.value) {
				AGENT_IMAGER_STARS_REFRESH_ITEM->sw.value = false;
				AGENT_IMAGER_STARS_PROPERTY->state = INDIGO_BUSY_STATE;
				indigo_update_property(device, AGENT_IMAGER_STARS_PROPERTY, NULL);
				indigo_set_timer(device, 0, find_stars_process, NULL);
			} else {
				for (int i = 1; i < AGENT_IMAGER_STARS_PROPERTY->count; i++) {
					if (AGENT_IMAGER_STARS_PROPERTY->items[i].sw.value) {
						int j = atoi(AGENT_IMAGER_STARS_PROPERTY->items[i].name);
						AGENT_IMAGER_SELECTION_X_ITEM->number.target = AGENT_IMAGER_SELECTION_X_ITEM->number.value = DEVICE_PRIVATE_DATA->stars[j].x;
						AGENT_IMAGER_SELECTION_Y_ITEM->number.target = AGENT_IMAGER_SELECTION_Y_ITEM->number.value = DEVICE_PRIVATE_DATA->stars[j].y;
						indigo_update_property(device, AGENT_IMAGER_SELECTION_PROPERTY, NULL);
						AGENT_IMAGER_STARS_PROPERTY->items[i].sw.value = false;
					}
				}
				AGENT_IMAGER_STARS_PROPERTY->state = INDIGO_OK_STATE;
			}
		}
		indigo_update_property(device, AGENT_IMAGER_STARS_PROPERTY, NULL);
	} else if (indigo_property_match(AGENT_IMAGER_SELECTION_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- AGENT_IMAGER_SELECTION
		if (FILTER_DEVICE_CONTEXT->running_process) {
			indigo_update_property(device, AGENT_IMAGER_SELECTION_PROPERTY, "Warning: Selection can not be changed while process is running!");
			return INDIGO_OK;
		}
		int count = AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM->number.value;
		indigo_property_copy_values(AGENT_IMAGER_SELECTION_PROPERTY, property, false);
		AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM->number.value = AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM->number.target = (int)AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM->number.target;
		if (count != AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM->number.value) {
			indigo_delete_property(device, AGENT_IMAGER_SELECTION_PROPERTY, NULL);
			AGENT_IMAGER_SELECTION_PROPERTY->count = (AGENT_IMAGER_SELECTION_X_ITEM - AGENT_IMAGER_SELECTION_PROPERTY->items) + 2 * AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM->number.value;
			for (int i = 0; i < AGENT_IMAGER_SELECTION_STAR_COUNT_ITEM->number.value; i++) {
				indigo_item *item_x = AGENT_IMAGER_SELECTION_X_ITEM + 2 * i;
				indigo_item *item_y = AGENT_IMAGER_SELECTION_Y_ITEM + 2 * i;
				item_x->number.value = item_x->number.target = 0;
				item_y->number.value = item_y->number.target = 0;
			}
			indigo_define_property(device, AGENT_IMAGER_SELECTION_PROPERTY, NULL);
		}
		save_config(device);
		AGENT_IMAGER_SELECTION_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, AGENT_IMAGER_SELECTION_PROPERTY, NULL);
	} else if (indigo_property_match(AGENT_START_PROCESS_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- AGENT_START_PROCESS
		if (AGENT_START_PROCESS_PROPERTY->state != INDIGO_BUSY_STATE && AGENT_IMAGER_STARS_PROPERTY->state != INDIGO_BUSY_STATE) {
			indigo_property_copy_values(AGENT_START_PROCESS_PROPERTY, property, false);
			if (!FILTER_CCD_LIST_PROPERTY->items->sw.value) {
				if (AGENT_IMAGER_START_PREVIEW_ITEM->sw.value) {
					AGENT_START_PROCESS_PROPERTY->state = INDIGO_BUSY_STATE;
					indigo_set_timer(device, 0, preview_process, NULL);
					indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
				} else if (AGENT_IMAGER_START_EXPOSURE_ITEM->sw.value) {
					AGENT_START_PROCESS_PROPERTY->state = INDIGO_BUSY_STATE;
					indigo_set_timer(device, 0, exposure_batch_process, NULL);
					indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
				} else if (AGENT_IMAGER_START_STREAMING_ITEM->sw.value) {
					AGENT_START_PROCESS_PROPERTY->state = INDIGO_BUSY_STATE;
					indigo_set_timer(device, 0, streaming_batch_process, NULL);
					indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
				} else if (AGENT_IMAGER_START_SEQUENCE_ITEM->sw.value) {
					AGENT_START_PROCESS_PROPERTY->state = INDIGO_BUSY_STATE;
					indigo_set_timer(device, 0, sequence_process, NULL);
					indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
				} else if (!FILTER_FOCUSER_LIST_PROPERTY->items->sw.value) {
					if (AGENT_IMAGER_START_FOCUSING_ITEM->sw.value) {
						AGENT_START_PROCESS_PROPERTY->state = INDIGO_BUSY_STATE;
						indigo_set_timer(device, 0, autofocus_process, NULL);
					}
					indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
				} else {
					AGENT_IMAGER_START_PREVIEW_ITEM->sw.value =
					AGENT_IMAGER_START_EXPOSURE_ITEM->sw.value =
					AGENT_IMAGER_START_STREAMING_ITEM->sw.value =
					AGENT_IMAGER_START_FOCUSING_ITEM->sw.value =
					AGENT_IMAGER_START_SEQUENCE_ITEM->sw.value = false;
					AGENT_START_PROCESS_PROPERTY->state = INDIGO_ALERT_STATE;
					indigo_update_property(device, AGENT_START_PROCESS_PROPERTY, "No focuser is selected");
				}
			} else {
				AGENT_IMAGER_START_PREVIEW_ITEM->sw.value =
				AGENT_IMAGER_START_EXPOSURE_ITEM->sw.value =
				AGENT_IMAGER_START_STREAMING_ITEM->sw.value =
				AGENT_IMAGER_START_FOCUSING_ITEM->sw.value =
				AGENT_IMAGER_START_SEQUENCE_ITEM->sw.value = false;
				AGENT_START_PROCESS_PROPERTY->state = INDIGO_ALERT_STATE;
				indigo_update_property(device, AGENT_START_PROCESS_PROPERTY, "No CCD is selected");
			}
		}
		AGENT_PAUSE_PROCESS_ITEM->sw.value = AGENT_PAUSE_PROCESS_WAIT_ITEM->sw.value = AGENT_PAUSE_PROCESS_AFTER_TRANSIT_ITEM->sw.value = false;
		AGENT_PAUSE_PROCESS_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, AGENT_PAUSE_PROCESS_PROPERTY, NULL);
		AGENT_ABORT_PROCESS_ITEM->sw.value = false;
		AGENT_ABORT_PROCESS_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, AGENT_ABORT_PROCESS_PROPERTY, NULL);
		indigo_update_property(device, AGENT_START_PROCESS_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(AGENT_PAUSE_PROCESS_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- AGENT_PAUSE_PROCESS
		if (AGENT_START_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
			indigo_property_copy_values(AGENT_PAUSE_PROCESS_PROPERTY, property, false);
			if (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
				AGENT_PAUSE_PROCESS_ITEM->sw.value = AGENT_PAUSE_PROCESS_WAIT_ITEM->sw.value = AGENT_PAUSE_PROCESS_AFTER_TRANSIT_ITEM->sw.value = false;
				AGENT_PAUSE_PROCESS_PROPERTY->state = INDIGO_OK_STATE;
			} else {
				if (AGENT_PAUSE_PROCESS_AFTER_TRANSIT_ITEM->sw.value) {
					AGENT_PAUSE_PROCESS_AFTER_TRANSIT_ITEM->sw.value = false; // can be only cleared when set by agent
				} else {
					AGENT_PAUSE_PROCESS_PROPERTY->state = INDIGO_BUSY_STATE;
					if (AGENT_PAUSE_PROCESS_ITEM->sw.value)
						abort_process(device);
				}
			}
		} else {
			AGENT_PAUSE_PROCESS_PROPERTY->state = INDIGO_ALERT_STATE;
		}
		indigo_update_property(device, AGENT_PAUSE_PROCESS_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(AGENT_ABORT_PROCESS_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- AGENT_ABORT_PROCESS
		if (AGENT_START_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE || AGENT_IMAGER_STARS_PROPERTY->state == INDIGO_BUSY_STATE) {
			indigo_property_copy_values(AGENT_ABORT_PROCESS_PROPERTY, property, false);
			if (AGENT_PAUSE_PROCESS_PROPERTY->state == INDIGO_BUSY_STATE) {
				AGENT_PAUSE_PROCESS_ITEM->sw.value = AGENT_PAUSE_PROCESS_WAIT_ITEM->sw.value =  false;
				AGENT_PAUSE_PROCESS_PROPERTY->state = INDIGO_ALERT_STATE;
				indigo_update_property(device, AGENT_PAUSE_PROCESS_PROPERTY, NULL);
			}
			AGENT_ABORT_PROCESS_PROPERTY->state = INDIGO_BUSY_STATE;
			abort_process(device);
		}
		AGENT_ABORT_PROCESS_ITEM->sw.value = false;
		indigo_update_property(device, AGENT_ABORT_PROCESS_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match(AGENT_PROCESS_FEATURES_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- AGENT_PROCESS_FEATURES
		indigo_property_copy_values(AGENT_PROCESS_FEATURES_PROPERTY, property, false);
		AGENT_PROCESS_FEATURES_PROPERTY->state = INDIGO_OK_STATE;
		save_config(device);
		indigo_update_property(device, AGENT_PROCESS_FEATURES_PROPERTY, NULL);
		return INDIGO_OK;
		// -------------------------------------------------------------------------------- AGENT_IMAGER_DOWNLOAD_FILE
	} else if (indigo_property_match(AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY, property)) {
		pthread_mutex_lock(&DEVICE_PRIVATE_DATA->mutex);
		indigo_property_copy_values(AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY, property, false);
		AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY->state = INDIGO_BUSY_STATE;
		indigo_update_property(device, AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY, NULL);
		AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY->state = INDIGO_ALERT_STATE;
		for (int i = 1; i < AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY->count; i++) {
			indigo_item *item = AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY->items + i;
			if (!strcmp(item->name, AGENT_IMAGER_DOWNLOAD_FILE_ITEM->text.value)) {
				char file_name[INDIGO_VALUE_SIZE + INDIGO_NAME_SIZE];
				struct stat file_stat;
				strcpy(file_name, DEVICE_PRIVATE_DATA->current_folder);
				strcat(file_name, AGENT_IMAGER_DOWNLOAD_FILE_ITEM->text.value);
				if (stat(file_name, &file_stat) < 0 || file_stat.st_size == 0) {
					AGENT_IMAGER_DOWNLOAD_IMAGE_PROPERTY->state = INDIGO_ALERT_STATE;
					indigo_update_property(device, AGENT_IMAGER_DOWNLOAD_IMAGE_PROPERTY, NULL);
					AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY->state = INDIGO_ALERT_STATE;
					break;
				}
				/* allocate 5% more mem to accomodate size fluctuation of the compressed images
				   and reallocate smaller buffer only if the next image is more than 50% smaller
				*/
				size_t malloc_size = 1.05 * file_stat.st_size;
				if (DEVICE_PRIVATE_DATA->image_buffer && (DEVICE_PRIVATE_DATA->image_buffer_size < file_stat.st_size || DEVICE_PRIVATE_DATA->image_buffer_size > 2 * file_stat.st_size)) {
					DEVICE_PRIVATE_DATA->image_buffer = indigo_safe_realloc(DEVICE_PRIVATE_DATA->image_buffer, malloc_size);
					DEVICE_PRIVATE_DATA->image_buffer_size = malloc_size;
				} else if (DEVICE_PRIVATE_DATA->image_buffer == NULL){
					DEVICE_PRIVATE_DATA->image_buffer = indigo_safe_malloc(malloc_size);
					DEVICE_PRIVATE_DATA->image_buffer_size = malloc_size;
				}
				int fd = open(file_name, O_RDONLY, 0);
				if (fd == -1) {
					break;
				}
				int result = indigo_read(fd, AGENT_IMAGER_DOWNLOAD_IMAGE_ITEM->blob.value = DEVICE_PRIVATE_DATA->image_buffer, AGENT_IMAGER_DOWNLOAD_IMAGE_ITEM->blob.size = file_stat.st_size);
				close(fd);
				if (result == -1) {
					AGENT_IMAGER_DOWNLOAD_IMAGE_PROPERTY->state = INDIGO_ALERT_STATE;
					indigo_update_property(device, AGENT_IMAGER_DOWNLOAD_IMAGE_PROPERTY, NULL);
					break;
				}
				*AGENT_IMAGER_DOWNLOAD_IMAGE_ITEM->blob.url = 0;
				*AGENT_IMAGER_DOWNLOAD_IMAGE_ITEM->blob.format = 0;
				char *file_type = strrchr(file_name, '.');
				if (file_type)
					strcpy(AGENT_IMAGER_DOWNLOAD_IMAGE_ITEM->blob.format, file_type);
				AGENT_IMAGER_DOWNLOAD_IMAGE_PROPERTY->state = INDIGO_OK_STATE;
				indigo_update_property(device, AGENT_IMAGER_DOWNLOAD_IMAGE_PROPERTY, NULL);
				AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY->state = INDIGO_OK_STATE;
				break;
			}
		}
		indigo_update_property(device, AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY, NULL);
		pthread_mutex_unlock(&DEVICE_PRIVATE_DATA->mutex);
		return INDIGO_OK;
		// -------------------------------------------------------------------------------- AGENT_IMAGER_DELETE_FILE
	} else if (indigo_property_match(AGENT_IMAGER_DELETE_FILE_PROPERTY, property)) {
		pthread_mutex_lock(&DEVICE_PRIVATE_DATA->mutex);
		indigo_property_copy_values(AGENT_IMAGER_DELETE_FILE_PROPERTY, property, false);
		AGENT_IMAGER_DELETE_FILE_PROPERTY->state = INDIGO_BUSY_STATE;
		indigo_update_property(device, AGENT_IMAGER_DELETE_FILE_PROPERTY, NULL);
		AGENT_IMAGER_DELETE_FILE_PROPERTY->state = INDIGO_ALERT_STATE;
		for (int i = 1; i < AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY->count; i++) {
			indigo_item *item = AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY->items + i;
			if (!strcmp(item->name, AGENT_IMAGER_DELETE_FILE_ITEM->text.value)) {
				char file_name[INDIGO_VALUE_SIZE + INDIGO_NAME_SIZE];
				struct stat file_stat;
				strcpy(file_name, DEVICE_PRIVATE_DATA->current_folder);
				strcat(file_name, AGENT_IMAGER_DELETE_FILE_ITEM->text.value);
				if (stat(file_name, &file_stat) < 0) {
					break;
				}
				indigo_update_property(device, AGENT_IMAGER_DELETE_FILE_PROPERTY, NULL);
				if (unlink(file_name) == -1) {
					break;
				}
				AGENT_IMAGER_DELETE_FILE_PROPERTY->state = INDIGO_OK_STATE;
				break;
			}
		}
		setup_download(device);
		indigo_update_property(device, AGENT_IMAGER_DELETE_FILE_PROPERTY, NULL);
		pthread_mutex_unlock(&DEVICE_PRIVATE_DATA->mutex);
		return INDIGO_OK;
	} else if (indigo_property_match(AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- AGENT_IMAGER_DOWNLOAD_FILES
		indigo_property_copy_values(AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY, property, false);
		if (AGENT_IMAGER_DOWNLOAD_FILES_REFRESH_ITEM->sw.value) {
			AGENT_IMAGER_DOWNLOAD_FILES_REFRESH_ITEM->sw.value = false;
			pthread_mutex_lock(&DEVICE_PRIVATE_DATA->mutex);
			setup_download(device);
			pthread_mutex_unlock(&DEVICE_PRIVATE_DATA->mutex);
		} else {
			pthread_mutex_lock(&DEVICE_PRIVATE_DATA->mutex);
			indigo_update_property(device, AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY, NULL);
			for (int i = 1; i < AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY->count; i++) {
				indigo_item *item = AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY->items + i;
				if (item->sw.value) {
					item->sw.value = false;
					AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY->state = INDIGO_OK_STATE;
					indigo_update_property(device, AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY, NULL);
					pthread_mutex_unlock(&DEVICE_PRIVATE_DATA->mutex);
					indigo_change_text_property_1(client, device->name, AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY_NAME, AGENT_IMAGER_DOWNLOAD_FILE_ITEM_NAME, item->name);
					return INDIGO_OK;
				}
			}
			pthread_mutex_unlock(&DEVICE_PRIVATE_DATA->mutex);
		}
		return INDIGO_OK;
	// -------------------------------------------------------------------------------- AGENT_WHEEL_FILTER
	} else if (indigo_property_match(AGENT_WHEEL_FILTER_PROPERTY, property)) {
		indigo_property_copy_values(AGENT_WHEEL_FILTER_PROPERTY, property, false);
		for (int i = 0; i < AGENT_WHEEL_FILTER_PROPERTY->count; i++) {
			if (AGENT_WHEEL_FILTER_PROPERTY->items[i].sw.value) {
				indigo_change_number_property_1(FILTER_DEVICE_CONTEXT->client, FILTER_DEVICE_CONTEXT->device_name[INDIGO_FILTER_WHEEL_INDEX], WHEEL_SLOT_PROPERTY_NAME, WHEEL_SLOT_ITEM_NAME, i + 1);
				break;
			}
		}
		AGENT_WHEEL_FILTER_PROPERTY->state = INDIGO_BUSY_STATE;
		indigo_update_property(device, AGENT_WHEEL_FILTER_PROPERTY,NULL);
		return INDIGO_OK;
	// -------------------------------------------------------------------------------- AGENT_IMAGER_SEQUENCE_SIZE
	} else if (indigo_property_match(AGENT_IMAGER_SEQUENCE_SIZE_PROPERTY, property)) {
		indigo_property_copy_values(AGENT_IMAGER_SEQUENCE_SIZE_PROPERTY, property, false);
		AGENT_IMAGER_SEQUENCE_SIZE_PROPERTY->state = INDIGO_OK_STATE;
		int old_size = AGENT_IMAGER_SEQUENCE_PROPERTY->count;
		int new_size = AGENT_IMAGER_SEQUENCE_SIZE_ITEM->number.value + 1;
		if (old_size != new_size) {
			indigo_delete_property(device, AGENT_IMAGER_SEQUENCE_PROPERTY, NULL);
			if (old_size < new_size) {
				for (int i = old_size; i < new_size; i++) {
					indigo_set_text_item_value(AGENT_IMAGER_SEQUENCE_PROPERTY->items + i, "");
				}
			}
			AGENT_IMAGER_SEQUENCE_PROPERTY->count = new_size;
			indigo_define_property(device, AGENT_IMAGER_SEQUENCE_PROPERTY, NULL);
			save_config(device);
		}
		indigo_update_property(device, AGENT_IMAGER_SEQUENCE_SIZE_PROPERTY, NULL);
		return INDIGO_OK;
	// -------------------------------------------------------------------------------- AGENT_IMAGER_SEQUENCE
	} else if (indigo_property_match(AGENT_IMAGER_SEQUENCE_PROPERTY, property)) {
		indigo_property_copy_values(AGENT_IMAGER_SEQUENCE_PROPERTY, property, false);
		AGENT_IMAGER_SEQUENCE_PROPERTY->state = INDIGO_OK_STATE;
		save_config(device);
		indigo_update_property(device, AGENT_IMAGER_SEQUENCE_PROPERTY, NULL);
		return INDIGO_OK;
		// -------------------------------------------------------------------------------- AGENT_IMAGER_BREAKPOINT
	} else if (indigo_property_match(AGENT_IMAGER_BREAKPOINT_PROPERTY, property)) {
		indigo_property_copy_values(AGENT_IMAGER_BREAKPOINT_PROPERTY, property, false);
		if (AGENT_IMAGER_RESUME_CONDITION_BARRIER_ITEM->sw.value) {
			// On related imager agents duplicate AGENT_IMAGER_BREAKPOINT_PROPERTY
			indigo_property *related_agents_property = FILTER_DEVICE_CONTEXT->filter_related_agent_list_property;
			indigo_property *clone = indigo_init_switch_property(NULL, AGENT_IMAGER_BREAKPOINT_PROPERTY->device, AGENT_IMAGER_BREAKPOINT_PROPERTY->name, NULL, NULL, 0, 0, 0, AGENT_IMAGER_BREAKPOINT_PROPERTY->count);
			memcpy(clone, AGENT_IMAGER_BREAKPOINT_PROPERTY, sizeof(indigo_property) + AGENT_IMAGER_BREAKPOINT_PROPERTY->count * sizeof(indigo_item));
			for (int i = 0; i < related_agents_property->count; i++) {
				indigo_item *item = related_agents_property->items + i;
				if (item->sw.value && !strncmp(item->name, "Imager Agent", 12)) {
					strcpy(clone->device, item->name);
					indigo_change_property(client, clone);
				}
			}
			indigo_release_property(clone);
		}
		AGENT_IMAGER_BREAKPOINT_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, AGENT_IMAGER_BREAKPOINT_PROPERTY, NULL);
		return INDIGO_OK;
		// -------------------------------------------------------------------------------- AGENT_IMAGER_RESUME_CONDITION
	} else if (indigo_property_match(AGENT_IMAGER_RESUME_CONDITION_PROPERTY, property)) {
		indigo_property_copy_values(AGENT_IMAGER_RESUME_CONDITION_PROPERTY, property, false);
		if (AGENT_IMAGER_RESUME_CONDITION_BARRIER_ITEM->sw.value) {
			// On related imager agents reset AGENT_IMAGER_RESUME_CONDITION_PROPERTY to AGENT_IMAGER_RESUME_CONDITION_TRIGGER_ITEM
			indigo_property *related_agents_property = FILTER_DEVICE_CONTEXT->filter_related_agent_list_property;
			for (int i = 0; i < related_agents_property->count; i++) {
				indigo_item *item = related_agents_property->items + i;
				if (item->sw.value && !strncmp(item->name, "Imager Agent", 12)) {
					indigo_change_switch_property_1(client, item->name, AGENT_IMAGER_RESUME_CONDITION_PROPERTY_NAME, AGENT_IMAGER_RESUME_CONDITION_TRIGGER_ITEM_NAME, true);
				}
			}
		}
		AGENT_IMAGER_RESUME_CONDITION_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, AGENT_IMAGER_RESUME_CONDITION_PROPERTY, NULL);
		return INDIGO_OK;
		// -------------------------------------------------------------------------------- ADDITIONAL_INSTANCES
	} else if (indigo_property_match(ADDITIONAL_INSTANCES_PROPERTY, property)) {
		if (indigo_filter_change_property(device, client, property) == INDIGO_OK) {
			save_config(device);
		}
		return INDIGO_OK;
	} else if (!strcmp(property->device, device->name)) {
		if (!strcmp(property->name, FOCUSER_BACKLASH_PROPERTY_NAME)) {
			AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.value = AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.target = property->items[0].number.value;
			indigo_update_property(device, AGENT_IMAGER_FOCUS_PROPERTY, NULL);
		} else if (!strcmp(property->name, CCD_SET_FITS_HEADER_PROPERTY_NAME)) {
			char *name = NULL;
			char *value = NULL;
			for (int i = 0; i < property->count; i++) {
				indigo_item *item = property->items + i;
				if (!strcmp(item->name, CCD_SET_FITS_HEADER_KEYWORD_ITEM_NAME)) {
					name = item->text.value;
				} else if (!strcmp(item->name, CCD_SET_FITS_HEADER_VALUE_ITEM_NAME)) {
					value = item->text.value;
				}
			}
			if (name != NULL && value != NULL) {
				int d, m, s;
				if (sscanf(value, "'%d %d %d'", &d, &m, &s) == 3) {
					double value = d + m / 60.0 + s / 3600.0;
					if (!strcmp(name, "OBJCTRA")) {
						DEVICE_PRIVATE_DATA->ra = value;
					} else if (!strcmp(name, "OBJCTDEC")) {
						DEVICE_PRIVATE_DATA->dec = value;
					} else if (!strcmp(name, "SITELAT")) {
						DEVICE_PRIVATE_DATA->latitude = value;
					} else if (!strcmp(name, "SITELONG")) {
						DEVICE_PRIVATE_DATA->longitude = value;
					}
					time_t utc = time(NULL);
					double lst = indigo_lst(&utc, DEVICE_PRIVATE_DATA->longitude);
					double ra = DEVICE_PRIVATE_DATA->ra;
					double dec = DEVICE_PRIVATE_DATA->dec;
					double transit;
					indigo_j2k_to_jnow(&ra, &dec);
					indigo_raise_set(UT2JD(utc), DEVICE_PRIVATE_DATA->latitude, DEVICE_PRIVATE_DATA->longitude, ra, dec, NULL, &transit, NULL);
					DEVICE_PRIVATE_DATA->time_to_transit = indigo_time_to_transit(ra, lst);
				}
			}
		}
	}
	return indigo_filter_change_property(device, client, property);
}

static indigo_result agent_device_detach(indigo_device *device) {
	assert(device != NULL);
	save_config(device);
	indigo_release_property(AGENT_IMAGER_BATCH_PROPERTY);
	indigo_release_property(AGENT_IMAGER_FOCUS_PROPERTY);
	indigo_release_property(AGENT_IMAGER_FOCUS_FAILURE_PROPERTY);
	indigo_release_property(AGENT_IMAGER_FOCUS_ESTIMATOR_PROPERTY);
	indigo_release_property(AGENT_IMAGER_DOWNLOAD_IMAGE_PROPERTY);
	indigo_release_property(AGENT_IMAGER_DOWNLOAD_FILE_PROPERTY);
	indigo_release_property(AGENT_IMAGER_DOWNLOAD_FILES_PROPERTY);
	indigo_release_property(AGENT_IMAGER_DELETE_FILE_PROPERTY);
	indigo_release_property(AGENT_IMAGER_STARS_PROPERTY);
	indigo_release_property(AGENT_IMAGER_SELECTION_PROPERTY);
	indigo_release_property(AGENT_IMAGER_STATS_PROPERTY);
	indigo_release_property(AGENT_START_PROCESS_PROPERTY);
	indigo_release_property(AGENT_PAUSE_PROCESS_PROPERTY);
	indigo_release_property(AGENT_ABORT_PROCESS_PROPERTY);
	indigo_release_property(AGENT_PROCESS_FEATURES_PROPERTY);
	indigo_release_property(AGENT_IMAGER_SEQUENCE_PROPERTY);
	indigo_release_property(AGENT_IMAGER_SEQUENCE_SIZE_PROPERTY);
	indigo_release_property(AGENT_IMAGER_BREAKPOINT_PROPERTY);
	indigo_release_property(AGENT_IMAGER_RESUME_CONDITION_PROPERTY);
	indigo_release_property(AGENT_IMAGER_BARRIER_STATE_PROPERTY);
	indigo_release_property(AGENT_WHEEL_FILTER_PROPERTY);
	pthread_mutex_destroy(&DEVICE_PRIVATE_DATA->mutex);
	indigo_safe_free(DEVICE_PRIVATE_DATA->image_buffer);
	DEVICE_PRIVATE_DATA->image_buffer_size = 0;
	indigo_safe_free(DEVICE_PRIVATE_DATA->last_image);
	DEVICE_PRIVATE_DATA->last_image_size = 0;
	return indigo_filter_device_detach(device);
}

// -------------------------------------------------------------------------------- INDIGO agent client implementation

static void snoop_guider_stats(indigo_client *client, indigo_property *property) {
	if (!strcmp(property->name, AGENT_GUIDER_STATS_PROPERTY_NAME)) {
		indigo_device *device = FILTER_CLIENT_CONTEXT->device;
		char *related_agent_name = indigo_filter_first_related_agent(device, "Guider Agent");
		if (related_agent_name && !strcmp(related_agent_name, property->device)) {
			int phase = 0;
			int frame = 0;
			for (int i = 0; i < property->count; i++) {
				indigo_item *item = property->items + i;
				if (!strcmp(item->name, AGENT_GUIDER_STATS_DITHERING_ITEM_NAME)) {
					AGENT_IMAGER_STATS_DITHERING_ITEM->number.value = item->number.value;
					indigo_update_property(device, AGENT_IMAGER_STATS_PROPERTY, NULL);
				} else if (!strcmp(item->name, AGENT_GUIDER_STATS_PHASE_ITEM_NAME)) {
					phase = (int)item->number.value;
				} else if (!strcmp(item->name, AGENT_GUIDER_STATS_FRAME_ITEM_NAME)) {
					frame = (int)item->number.value;
				}
			}
			DEVICE_PRIVATE_DATA->guiding = (phase == INDIGO_GUIDER_PHASE_GUIDING) && (frame > 5);
		}
	}
}

static void snoop_guider_dithering_state(indigo_client *client, indigo_property *property) {
	if (!strcmp(property->name, AGENT_GUIDER_DITHER_PROPERTY_NAME)) {
		indigo_device *device = FILTER_CLIENT_CONTEXT->device;
		char *related_agent_name = indigo_filter_first_related_agent(device, "Guider Agent");
		if (related_agent_name && !strcmp(related_agent_name, property->device)) {
			for (int i = 0; i < property->count; i++) {
				indigo_item *item = property->items + i;
				if (!strcmp(item->name, AGENT_GUIDER_DITHER_TRIGGER_ITEM_NAME)) {
					if (!DEVICE_PRIVATE_DATA->dithering_finished) {
						if (item->sw.value && property->state == INDIGO_BUSY_STATE && !DEVICE_PRIVATE_DATA->dithering_started) {
							DEVICE_PRIVATE_DATA->dithering_started = true;
						} else if (property->state == INDIGO_OK_STATE && DEVICE_PRIVATE_DATA->dithering_started) {
							DEVICE_PRIVATE_DATA->dithering_finished = true;
						} else if (property->state == INDIGO_ALERT_STATE) {
							DEVICE_PRIVATE_DATA->dithering_started = true;
							DEVICE_PRIVATE_DATA->dithering_finished = true;
						}
					}
					break;
				}
			}
		}
	}
}

static void snoop_barrier_state(indigo_client *client, indigo_property *property) {
	if (!strcmp(property->name, AGENT_PAUSE_PROCESS_PROPERTY_NAME)) {
		indigo_device *device = FILTER_CLIENT_CONTEXT->device;
		char *related_agent_name = indigo_filter_first_related_agent(device, property->device);
		if (related_agent_name) {
			CLIENT_PRIVATE_DATA->barrier_resume = true;
			for (int i = 0; i < AGENT_IMAGER_BARRIER_STATE_PROPERTY->count; i++) {
				indigo_item *item = AGENT_IMAGER_BARRIER_STATE_PROPERTY->items + i;
				if (!strcmp(item->name, property->device)) {
					item->light.value = property->state;
					indigo_update_property(device, AGENT_IMAGER_BARRIER_STATE_PROPERTY, NULL);
				}
				CLIENT_PRIVATE_DATA->barrier_resume &= (item->light.value == INDIGO_BUSY_STATE);
			}
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Breakpoint barrier state %s", CLIENT_PRIVATE_DATA->barrier_resume ? "complete" : "incomplete");
		}
	}
}

static void snoop_wheel_changes(indigo_client *client, indigo_property *property) {
	if (*FILTER_CLIENT_CONTEXT->device_name[INDIGO_FILTER_WHEEL_INDEX] && !strcmp(property->device, FILTER_CLIENT_CONTEXT->device_name[INDIGO_FILTER_WHEEL_INDEX])) {
		if (!strcmp(property->name, WHEEL_SLOT_NAME_PROPERTY_NAME)) {
			indigo_property *agent_wheel_filter_property = CLIENT_PRIVATE_DATA->agent_wheel_filter_property;
			agent_wheel_filter_property->count = property->count;
			for (int i = 0; i < property->count; i++)
				strcpy(agent_wheel_filter_property->items[i].label, property->items[i].text.value);
			indigo_delete_property(FILTER_CLIENT_CONTEXT->device, agent_wheel_filter_property, NULL);
			agent_wheel_filter_property->hidden = false;
			indigo_define_property(FILTER_CLIENT_CONTEXT->device, agent_wheel_filter_property, NULL);
		} else if (!strcmp(property->name, WHEEL_SLOT_PROPERTY_NAME)) {
			indigo_property *agent_wheel_filter_property = CLIENT_PRIVATE_DATA->agent_wheel_filter_property;
			int value = property->items->number.value;
			if (value)
				indigo_set_switch(agent_wheel_filter_property, agent_wheel_filter_property->items + value - 1, true);
			else
				indigo_set_switch(agent_wheel_filter_property, agent_wheel_filter_property->items, false);
			agent_wheel_filter_property->state = property->state;
			indigo_update_property(FILTER_CLIENT_CONTEXT->device, agent_wheel_filter_property, NULL);
		}
	}
}

static void snoop_solver_process_state(indigo_client *client, indigo_property *property) {
	if (!strcmp(property->name, AGENT_START_PROCESS_PROPERTY_NAME)) {
		char *related_agent_name = indigo_filter_first_related_agent(FILTER_CLIENT_CONTEXT->device, "Astrometry Agent");
		if (related_agent_name && !strcmp(property->device, related_agent_name)) {
			CLIENT_PRIVATE_DATA->related_solver_process_state = property->state;
			return;
		}
		related_agent_name = indigo_filter_first_related_agent(FILTER_CLIENT_CONTEXT->device, "ASTAP Agent");
		if (related_agent_name && !strcmp(property->device, related_agent_name)) {
			CLIENT_PRIVATE_DATA->related_solver_process_state = property->state;
			return;
		}
	}
}

static void snoop_guider_process_state(indigo_client *client, indigo_property *property) {
	if (!strcmp(property->name, AGENT_START_PROCESS_PROPERTY_NAME)) {
		char *agent = indigo_filter_first_related_agent(FILTER_CLIENT_CONTEXT->device, "Guider Agent");
		if (agent && !strcmp(property->device, agent)) {
			CLIENT_PRIVATE_DATA->related_guider_process_state = property->state;
		}
	}
}

static indigo_result agent_define_property(indigo_client *client, indigo_device *device, indigo_property *property, const char *message) {
	if (*FILTER_CLIENT_CONTEXT->device_name[INDIGO_FILTER_CCD_INDEX] && !strcmp(property->device, FILTER_CLIENT_CONTEXT->device_name[INDIGO_FILTER_CCD_INDEX])) {
		if (property->state == INDIGO_OK_STATE && !strcmp(property->name, CCD_LOCAL_MODE_PROPERTY_NAME)) {
			*CLIENT_PRIVATE_DATA->current_folder = 0;
			for (int i = 0; i < property->count; i++) {
				indigo_item *item = property->items + i;
				if (strcmp(item->name, CCD_LOCAL_MODE_DIR_ITEM_NAME) == 0) {
					indigo_copy_value(CLIENT_PRIVATE_DATA->current_folder, item->text.value);
					break;
				}
			}
			pthread_mutex_lock(&CLIENT_PRIVATE_DATA->mutex);
			setup_download(FILTER_CLIENT_CONTEXT->device);
			pthread_mutex_unlock(&CLIENT_PRIVATE_DATA->mutex);
		} else if (property->state == INDIGO_OK_STATE && !strcmp(property->name, CCD_BIN_PROPERTY_NAME)) {
			for (int i = 0; i < property->count; i++) {
				indigo_item *item = property->items + i;
				if (strcmp(item->name, CCD_BIN_HORIZONTAL_ITEM_NAME) == 0) {
					CLIENT_PRIVATE_DATA->bin_x = item->number.value;
				} else if (strcmp(item->name, CCD_BIN_VERTICAL_ITEM_NAME) == 0) {
					CLIENT_PRIVATE_DATA->bin_y = item->number.value;
				}
			}
		}
	} else if (*FILTER_CLIENT_CONTEXT->device_name[INDIGO_FILTER_FOCUSER_INDEX] && !strcmp(property->device, FILTER_CLIENT_CONTEXT->device_name[INDIGO_FILTER_FOCUSER_INDEX])) {
		if (!strcmp(property->name, FOCUSER_POSITION_PROPERTY_NAME)) {
			CLIENT_PRIVATE_DATA->focuser_position = property->items[0].number.value;
			INDIGO_DRIVER_DEBUG(DRIVER_NAME,"focuser_position = %f", property->items[0].number.value);
		} else if (!strcmp(property->name, FOCUSER_BACKLASH_PROPERTY_NAME)) {
			indigo_device *device = FILTER_CLIENT_CONTEXT->device;
			DEVICE_PRIVATE_DATA->focuser_has_backlash = true;
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "focuser_has_backlash = %d", DEVICE_PRIVATE_DATA->focuser_has_backlash);
			AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.value = AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.target = property->items[0].number.value;
			indigo_update_property(device, AGENT_IMAGER_FOCUS_PROPERTY, NULL);
		}
	} else {
		snoop_wheel_changes(client, property);
		snoop_guider_stats(client, property);
		snoop_guider_dithering_state(client, property);
		snoop_barrier_state(client, property);
		snoop_solver_process_state(client, property);
		snoop_guider_process_state(client, property);
	}
	return indigo_filter_define_property(client, device, property, message);
}

static indigo_result agent_update_property(indigo_client *client, indigo_device *device, indigo_property *property, const char *message) {
	if (device == FILTER_CLIENT_CONTEXT->device) {
		if (property->state == INDIGO_OK_STATE && !strcmp(property->name, FILTER_RELATED_AGENT_LIST_PROPERTY_NAME)) {
			AGENT_IMAGER_BARRIER_STATE_PROPERTY->count = 0;
			indigo_property *clone = indigo_init_switch_property(NULL, AGENT_IMAGER_BREAKPOINT_PROPERTY->device, AGENT_IMAGER_BREAKPOINT_PROPERTY->name, NULL, NULL, 0, 0, 0, AGENT_IMAGER_BREAKPOINT_PROPERTY->count);
			memcpy(clone, AGENT_IMAGER_BREAKPOINT_PROPERTY, sizeof(indigo_property) + AGENT_IMAGER_BREAKPOINT_PROPERTY->count * sizeof(indigo_item));
			for (int i = 0; i < property->count; i++) {
				indigo_item *item = property->items + i;
				if (item->sw.value && !strncmp(item->name, "Imager Agent", 12)) {
					AGENT_IMAGER_BARRIER_STATE_PROPERTY = indigo_resize_property(AGENT_IMAGER_BARRIER_STATE_PROPERTY, AGENT_IMAGER_BARRIER_STATE_PROPERTY->count + 1);
					indigo_init_light_item(AGENT_IMAGER_BARRIER_STATE_PROPERTY->items + AGENT_IMAGER_BARRIER_STATE_PROPERTY->count - 1, item->name, item->label, INDIGO_IDLE_STATE);
					if (AGENT_IMAGER_RESUME_CONDITION_BARRIER_ITEM->sw.value) {
						// On related imager agents duplicate AGENT_IMAGER_BREAKPOINT_PROPERTY and reset AGENT_IMAGER_RESUME_CONDITION_PROPERTY to AGENT_IMAGER_RESUME_CONDITION_TRIGGER_ITEM
						strcpy(clone->device, item->name);
						indigo_change_property(client, clone);
						indigo_change_switch_property_1(client, item->name, AGENT_IMAGER_RESUME_CONDITION_PROPERTY_NAME, AGENT_IMAGER_RESUME_CONDITION_TRIGGER_ITEM_NAME, true);
					}
				}
			}
			indigo_release_property(clone);
			indigo_delete_property(device, AGENT_IMAGER_BARRIER_STATE_PROPERTY, NULL);
			indigo_define_property(device, AGENT_IMAGER_BARRIER_STATE_PROPERTY, NULL);
			indigo_property property = { 0 };
			strcpy(property.name, AGENT_PAUSE_PROCESS_PROPERTY_NAME);
			for (int i = 0; i < AGENT_IMAGER_BARRIER_STATE_PROPERTY->count; i++) {
				indigo_item *item = AGENT_IMAGER_BARRIER_STATE_PROPERTY->items + i;
				strcpy(property.device, item->name);
				indigo_enumerate_properties(client, &property);
			}
		}
	} else if (*FILTER_CLIENT_CONTEXT->device_name[INDIGO_FILTER_CCD_INDEX] && !strcmp(property->device, FILTER_CLIENT_CONTEXT->device_name[INDIGO_FILTER_CCD_INDEX])) {
		if (property->state == INDIGO_OK_STATE && !strcmp(property->name, CCD_IMAGE_PROPERTY_NAME)) {
			if (strchr(property->device, '@'))
				indigo_populate_http_blob_item(property->items);
			if (property->items->blob.value) {
				CLIENT_PRIVATE_DATA->last_image = indigo_safe_realloc(CLIENT_PRIVATE_DATA->last_image, property->items->blob.size);
				memcpy(CLIENT_PRIVATE_DATA->last_image, property->items->blob.value, property->items->blob.size);
				CLIENT_PRIVATE_DATA->last_image_size = property->items->blob.size;
			} else if (CLIENT_PRIVATE_DATA->last_image) {
				free(CLIENT_PRIVATE_DATA->last_image);
				CLIENT_PRIVATE_DATA->last_image_size = 0;
				CLIENT_PRIVATE_DATA->last_image = NULL;
			}
		} else if (property->state == INDIGO_OK_STATE && !strcmp(property->name, CCD_IMAGE_FILE_PROPERTY_NAME)) {
			pthread_mutex_lock(&CLIENT_PRIVATE_DATA->mutex);
			setup_download(FILTER_CLIENT_CONTEXT->device);
			pthread_mutex_unlock(&CLIENT_PRIVATE_DATA->mutex);
		} else if (property->state == INDIGO_OK_STATE && !strcmp(property->name, CCD_LOCAL_MODE_PROPERTY_NAME)) {
			*CLIENT_PRIVATE_DATA->current_folder = 0;
			for (int i = 0; i < property->count; i++) {
				indigo_item *item = property->items + i;
				if (strcmp(item->name, CCD_LOCAL_MODE_DIR_ITEM_NAME) == 0) {
					indigo_copy_value(CLIENT_PRIVATE_DATA->current_folder, item->text.value);
					break;
				}
			}
			pthread_mutex_lock(&CLIENT_PRIVATE_DATA->mutex);
			setup_download(FILTER_CLIENT_CONTEXT->device);
			pthread_mutex_unlock(&CLIENT_PRIVATE_DATA->mutex);
		} else if (property->state == INDIGO_OK_STATE && !strcmp(property->name, CCD_BIN_PROPERTY_NAME)) {
			indigo_property *agent_selection_property = CLIENT_PRIVATE_DATA->agent_selection_property;
			for (int i = 0; i < property->count; i++) {
				indigo_item *item = property->items + i;
				if (strcmp(item->name, CCD_BIN_HORIZONTAL_ITEM_NAME) == 0) {
					double ratio = CLIENT_PRIVATE_DATA->bin_x / item->number.target;
					agent_selection_property->items[0].number.value = agent_selection_property->items[0].number.target *= ratio;
					CLIENT_PRIVATE_DATA->bin_x = item->number.value;
				} else if (strcmp(item->name, CCD_BIN_VERTICAL_ITEM_NAME) == 0) {
					double ratio = CLIENT_PRIVATE_DATA->bin_y / item->number.target;
					agent_selection_property->items[1].number.value = agent_selection_property->items[1].number.target *= ratio;
					CLIENT_PRIVATE_DATA->bin_y = item->number.value;
				}
			}
			indigo_update_property(FILTER_CLIENT_CONTEXT->device, agent_selection_property, NULL);
		}
	} else if (*FILTER_CLIENT_CONTEXT->device_name[INDIGO_FILTER_WHEEL_INDEX] && !strcmp(property->device, FILTER_CLIENT_CONTEXT->device_name[INDIGO_FILTER_WHEEL_INDEX])) {
		if (!strcmp(property->name, WHEEL_SLOT_NAME_PROPERTY_NAME)) {
			indigo_property *agent_wheel_filter_property = CLIENT_PRIVATE_DATA->agent_wheel_filter_property;
			agent_wheel_filter_property->count = property->count;
			for (int i = 0; i < property->count; i++)
				strcpy(agent_wheel_filter_property->items[i].label, property->items[i].text.value);
			agent_wheel_filter_property->hidden = false;
			indigo_delete_property(FILTER_CLIENT_CONTEXT->device, agent_wheel_filter_property, NULL);
			indigo_define_property(FILTER_CLIENT_CONTEXT->device, agent_wheel_filter_property, NULL);
		} else if (!strcmp(property->name, WHEEL_SLOT_PROPERTY_NAME)) {
			indigo_property *agent_wheel_filter_property = CLIENT_PRIVATE_DATA->agent_wheel_filter_property;
			int value = property->items->number.value;
			if (value)
				indigo_set_switch(agent_wheel_filter_property, agent_wheel_filter_property->items + value - 1, true);
			else
				indigo_set_switch(agent_wheel_filter_property, agent_wheel_filter_property->items, false);
			agent_wheel_filter_property->state = property->state;
			indigo_update_property(FILTER_CLIENT_CONTEXT->device, agent_wheel_filter_property, NULL);
		}
	} else if (*FILTER_CLIENT_CONTEXT->device_name[INDIGO_FILTER_FOCUSER_INDEX] && !strcmp(property->device, FILTER_CLIENT_CONTEXT->device_name[INDIGO_FILTER_FOCUSER_INDEX])) {
		if (!strcmp(property->name, FOCUSER_POSITION_PROPERTY_NAME)) {
			CLIENT_PRIVATE_DATA->focuser_position = property->items[0].number.value;
		} else if (!strcmp(property->name, FOCUSER_BACKLASH_PROPERTY_NAME)) {
			indigo_device *device = FILTER_CLIENT_CONTEXT->device;
			DEVICE_PRIVATE_DATA->focuser_has_backlash = true;
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "focuser_has_backlash = %d", DEVICE_PRIVATE_DATA->focuser_has_backlash);
			AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.value = AGENT_IMAGER_FOCUS_BACKLASH_ITEM->number.target = property->items[0].number.value;
			indigo_update_property(device, AGENT_IMAGER_FOCUS_PROPERTY, NULL);
		}
	} else {
		snoop_wheel_changes(client, property);
		snoop_guider_stats(client, property);
		snoop_guider_dithering_state(client, property);
		snoop_barrier_state(client, property);
		snoop_solver_process_state(client, property);
		snoop_guider_process_state(client, property);
	}
	return indigo_filter_update_property(client, device, property, message);
}

static indigo_result agent_delete_property(indigo_client *client, indigo_device *device, indigo_property *property, const char *message) {
	if (!strcmp(property->device, IMAGER_AGENT_NAME) && (!strcmp(property->name, CCD_LOCAL_MODE_PROPERTY_NAME) || !strcmp(property->name, CCD_IMAGE_FORMAT_PROPERTY_NAME))) {
		*CLIENT_PRIVATE_DATA->current_folder = 0;
		pthread_mutex_lock(&CLIENT_PRIVATE_DATA->mutex);
		setup_download(FILTER_CLIENT_CONTEXT->device);
		pthread_mutex_unlock(&CLIENT_PRIVATE_DATA->mutex);
	} else if (!strcmp(property->device, IMAGER_AGENT_NAME) && !strcmp(property->name, WHEEL_SLOT_PROPERTY_NAME)) {
		indigo_delete_property(FILTER_CLIENT_CONTEXT->device, CLIENT_PRIVATE_DATA->agent_wheel_filter_property, NULL);
		CLIENT_PRIVATE_DATA->agent_wheel_filter_property->hidden = true;
	} else if (!strcmp(property->device, IMAGER_AGENT_NAME) && (!strcmp(property->name, FOCUSER_BACKLASH_PROPERTY_NAME) || !strcmp(property->name, ""))) {
		DEVICE_PRIVATE_DATA->focuser_has_backlash = false;
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "focuser_has_backlash = %d", DEVICE_PRIVATE_DATA->focuser_has_backlash);
	}
	return indigo_filter_delete_property(client, device, property, message);
}
// -------------------------------------------------------------------------------- Initialization

static agent_private_data *private_data = NULL;

static indigo_device *agent_device = NULL;
static indigo_client *agent_client = NULL;

indigo_result indigo_agent_imager(indigo_driver_action action, indigo_driver_info *info) {
	static indigo_device agent_device_template = INDIGO_DEVICE_INITIALIZER(
		IMAGER_AGENT_NAME,
		agent_device_attach,
		agent_enumerate_properties,
		agent_change_property,
		NULL,
		agent_device_detach
	);

	static indigo_client agent_client_template = {
		IMAGER_AGENT_NAME, false, NULL, INDIGO_OK, INDIGO_VERSION_CURRENT, NULL,
		indigo_filter_client_attach,
		agent_define_property,
		agent_update_property,
		agent_delete_property,
		NULL,
		indigo_filter_client_detach
	};

	static indigo_driver_action last_action = INDIGO_DRIVER_SHUTDOWN;

	SET_DRIVER_INFO(info, IMAGER_AGENT_NAME, __FUNCTION__, DRIVER_VERSION, false, last_action);

	if (action == last_action)
		return INDIGO_OK;

	switch(action) {
		case INDIGO_DRIVER_INIT:
			last_action = action;
			private_data = indigo_safe_malloc(sizeof(agent_private_data));
			agent_device = indigo_safe_malloc_copy(sizeof(indigo_device), &agent_device_template);
			agent_device->private_data = private_data;
			indigo_attach_device(agent_device);

			agent_client = indigo_safe_malloc_copy(sizeof(indigo_client), &agent_client_template);
			agent_client->client_context = agent_device->device_context;
			indigo_attach_client(agent_client);
			break;

		case INDIGO_DRIVER_SHUTDOWN:
			last_action = action;
			if (agent_client != NULL) {
				indigo_detach_client(agent_client);
				free(agent_client);
				agent_client = NULL;
			}
			if (agent_device != NULL) {
				indigo_detach_device(agent_device);
				free(agent_device);
				agent_device = NULL;
			}
			if (private_data != NULL) {
				free(private_data);
				private_data = NULL;
			}
			break;

		case INDIGO_DRIVER_INFO:
			break;
	}
	return INDIGO_OK;
}
