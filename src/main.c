/**
 * Copyright © 2024 Fraawlen <fraawlen@posteo.net>
 *
 * This file is part of the program.
 *
 * This library is free software; you can redistribute it and/or modify it either under the terms of the GNU
 * Affero General Public License as published by the Free Software Foundation; either version 3.0  of the
 * License or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.
 * See the LGPL for the specific language governing rights and limitations.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this program. If not,
 * see <http://www.gnu.org/licenses/>.
 */

/************************************************************************************************************/
/************************************************************************************************************/
/************************************************************************************************************/

#include <dg/core/core.h>
#include <dg/base/base.h>
#include <dg/base/origin.h>
#include <dg/base/string.h>
#include <float.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/sysinfo.h>
#include <unistd.h>

/************************************************************************************************************/
/************************************************************************************************************/
/************************************************************************************************************/

#define PROGRAM "sysgauges"
#define VERSION "v.1.0.0"
#define GB      * data.mem_unit / 1073741824.0

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

struct row
{
	const char *name;
	const char *unit;
	const int precision;
	const bool custom_max;
	dg_core_cell_t *label;
	dg_core_cell_t *gauge;
	dg_core_cell_t *max;
};

/************************************************************************************************************/
/************************************************************************************************************/
/************************************************************************************************************/

static void  help        (void);
static void  options     (int, char**);
static void  resize      (void);
static void  row_destroy (struct row *);
static void  row_setup   (struct row *, double);
static void  row_update  (struct row *, double, double);
static void *thread      (void *);
static void  update_all  (uint32_t);

/************************************************************************************************************/
/************************************************************************************************************/
/************************************************************************************************************/

/* - User parameters - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static bool         show_max = false;
static bool         verbose  = false;
static double       alert    = 0.95;
static unsigned int delay    = 1;
static int16_t      width    = 0;
static int16_t      x        = 20;
static int16_t      y        = 20;

/* GUI components  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static dg_core_window_t *window = NULL;
static dg_core_grid_t   *grid   = NULL;

static struct row cpu = { "CPU", "%",  2, false, NULL, NULL, NULL };
static struct row mem = { "MEM", "GB", 1, true,  NULL, NULL, NULL };
static struct row swp = { "SWP", "GB", 1, true,  NULL, NULL, NULL };

static int16_t pos = 0;

/************************************************************************************************************/
/* MAIN *****************************************************************************************************/
/************************************************************************************************************/

int
main(int argc, char **argv)
{
	struct sysinfo data;
	pthread_t t;

	/* Setup */

	dg_core_init(argc, argv, NULL, NULL, NULL);
	dg_base_init();
	sysinfo(&data);
	options(argc, argv);
	pthread_create(&t, NULL, thread, NULL);

	window = dg_core_window_create(DG_CORE_WINDOW_FIXED);
	grid   = dg_core_grid_create(3, data.totalswap > 0 ? 3 : 2);

	/* Grid configuration */

	dg_core_grid_set_column_growth(grid, 1, 1.0);
	dg_core_grid_set_column_width(grid, 0, 3);
	dg_core_grid_set_column_width(grid, 1, 32);
	dg_core_grid_set_column_width(grid, 2, 6);

	/* Rows configuration */

	row_setup(&cpu, 100.0);
	row_setup(&mem, data.totalram  GB);
	row_setup(&swp, data.totalswap GB);

	/* Window configuration */

	resize();

	dg_core_window_push_grid(window, grid);
	dg_core_window_rename(window, "sysmeter", NULL);
	dg_core_window_activate(window);
	dg_core_window_set_fixed_position(window, x, y);

	/* Run */

	dg_core_resource_set_callback(resize);
	dg_core_loop_set_callback_signal(update_all);
	dg_core_loop_run();

	/* Cleanup & end */

	dg_core_window_destroy(window);
	dg_core_grid_destroy(grid);

	row_destroy(&cpu);
	row_destroy(&mem);
	row_destroy(&swp);

	dg_base_reset();
	dg_core_reset();

	pthread_join(t, NULL);

	return 0;
}

/************************************************************************************************************/
/* STATIC ***************************************************************************************************/
/************************************************************************************************************/

static void
help(void)
{
	printf(
		PROGRAM " " VERSION "\n"
		"usage: " PROGRAM " [option] <value>\n"
		"\t-a <0.0..1.0> : alert threshold\n"
		"\t-h            : print this help\n"
		"\t-i <uint>     : update interval in seconds\n"
		"\t-m            : show max MEM and SWP values\n"
		"\t-v            : print extra information (window width and height)\n"
		"\t-w <uint16_t> : custom width\n"
		"\t-x <int16_t>  : custom x coordinate\n"
		"\t-y <int16_t>  : custom y coordinate\n");
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static void
options(int argc, char **argv)
{
	int opt;

	while ((opt = getopt(argc, argv, "a:hi:mvw:x:y:")) != -1)
	{
		switch (opt)
		{
			case 'a':
				alert = strtod(optarg, NULL);
				break;

			case 'h':
				help();
				exit(0);

			case 'i':
				delay = strtoul(optarg, NULL, 0);
				break;

			case 'm':
				show_max = true;
				break;

			case 'v':
				verbose = true;
				break;

			case 'w':
				width = strtoul(optarg, NULL, 0);
				break;

			case 'x':
				x = strtol(optarg, NULL, 0);
				break;

			case 'y':
				y = strtol(optarg, NULL, 0);
				break;

			case '?':
			default :
				fprintf(stderr, "try \'" PROGRAM " -h\' for more information\n");
				exit(0);
		}
	}
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static void
resize(void)
{
	int16_t w = dg_core_grid_get_min_pixel_width(grid);
	int16_t h = dg_core_grid_get_min_pixel_height(grid);

	dg_core_window_set_fixed_size(window, width > w ? width : w, h);

	if (verbose)
	{
		printf("window size updated\n");
		printf("width  = %i\n", w);
		printf("height = %i\n", h);
	}
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static void
row_destroy(struct row *r)
{
	dg_core_cell_destroy(r->label);
	dg_core_cell_destroy(r->gauge);
	dg_core_cell_destroy(r->max);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static void
row_setup(struct row *r, double max)
{
	dg_base_string_t tmp;

	r->label = dg_base_indicator_create();
	r->gauge = dg_base_gauge_create();
	r->max   = dg_base_label_create();

	if (max <= DBL_EPSILON)
	{
		return;
	}

	tmp = dg_base_string_convert_double(max, r->precision);

	dg_base_string_append(&tmp, r->unit);
	dg_base_gauge_set_label_style(r->gauge, r->precision, r->unit);
	dg_base_gauge_set_limits(r->gauge, 0.0, max);
	dg_base_indicator_set_label(r->label, r->name);
	dg_base_label_set_label(r->max, tmp.chars);
	dg_base_label_set_origin(r->max, DG_BASE_ORIGIN_RIGHT);
	dg_base_string_clear(&tmp);

	dg_core_grid_assign_cell(grid, r->label, 0, pos, 1, 1);
	if (show_max && r->custom_max)
	{
		dg_core_grid_assign_cell(grid, r->gauge, 1, pos, 1, 1);
		dg_core_grid_assign_cell(grid, r->max,   2, pos, 1, 1);
	}
	else
	{
		dg_core_grid_assign_cell(grid, r->gauge, 1, pos, 2, 1);
	}

	pos++;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static void
row_update(struct row *r, double val, double high)
{
	dg_base_gauge_set_value(r->gauge, val);

	if (val >= high)
	{
		dg_base_indicator_set_on(r->label);
	}
	else
	{
		dg_base_indicator_set_off(r->label);
	}
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static void *
thread(void *params)
{
	(void)params;

	while (dg_core_is_init())
	{
		dg_core_loop_send_signal(0);
		sleep(delay);
	}

	pthread_exit(NULL);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static void
update_all(uint32_t serial)
{
	struct sysinfo data;

	(void)serial;

	sysinfo(&data);

	row_update(&cpu, data.loads[0] * 100.0 / get_nprocs() / (1 << SI_LOAD_SHIFT), 100.0 * alert);
	row_update(&mem, (data.totalram  - data.freeram)  GB, data.totalram  GB * alert);
	row_update(&swp, (data.totalswap - data.freeswap) GB, data.totalswap GB * alert);
}
