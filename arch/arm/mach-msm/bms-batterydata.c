/* Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/batterydata-lib.h>
#ifdef ASUS_A500KL_PROJECT

static struct single_row_lut A500KL_fcc_temp = {

	.x		= {-20, 0, 25, 40, 60},

	.y		= {2050, 2050, 2050, 2050, 2050},

	.cols	= 5

};



static struct single_row_lut A500KL_fcc_sf = {

	.x		= {0},

	.y		= {100},

	.cols	= 1

};



static struct sf_lut A500KL_rbatt_sf = {

	.rows		= 30,

	.cols		= 5,

	.row_entries		= {-20, 0, 25, 40, 60},

	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 16, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1},

	.sf		= {

				{1280, 334, 100, 75, 68},

				{1234, 335, 100, 77, 69},

				{1236, 335, 100, 77, 69},

				{1192, 337, 103, 78, 70},

				{1150, 338, 106, 80, 72},

				{1127, 335, 111, 83, 74},

				{1118, 324, 120, 88, 76},

				{1119, 316, 131, 94, 81},

				{1133, 311, 130, 105, 88},

				{1156, 309, 103, 81, 72},

				{1180, 312, 100, 77, 70},

				{1212, 319, 102, 79, 72},

				{1254, 334, 104, 83, 75},

				{1312, 357, 108, 86, 79},

				{1391, 380, 113, 83, 72},

				{1495, 400, 115, 81, 71},

				{1839, 421, 118, 83, 72},

				{2326, 455, 118, 82, 72},

				{2875, 490, 120, 83, 74},

				{3300, 503, 126, 88, 76},

				{3411, 490, 127, 90, 77},

				{3467, 481, 134, 92, 78},

				{3971, 495, 143, 95, 77},

				{4681, 508, 150, 96, 81},

				{5655, 520, 151, 104, 87},

				{7006, 545, 154, 111, 93},

				{9028, 588, 154, 119, 100},

				{12202, 663, 161, 125, 106},

				{17223, 819, 182, 136, 117},

				{25932, 1457, 241, 177, 176},

	}

};



static struct pc_temp_ocv_lut A500KL_pc_temp_ocv = {

	.rows		= 31,

	.cols		= 5,

	.temp		= {-20, 0, 25, 40, 60},

	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 16, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},

	.ocv		= {

				{4328, 4328, 4323, 4317, 4309},

				{4204, 4243, 4252, 4250, 4246},

				{4122, 4182, 4193, 4193, 4189},

				{4051, 4126, 4138, 4138, 4134},

				{3983, 4072, 4086, 4085, 4082},

				{3925, 4019, 4037, 4037, 4033},

				{3876, 3966, 3992, 3992, 3989},

				{3835, 3918, 3952, 3952, 3949},

				{3804, 3878, 3907, 3912, 3910},

				{3776, 3843, 3856, 3858, 3858},

				{3747, 3814, 3826, 3829, 3829},

				{3718, 3791, 3805, 3807, 3807},

				{3689, 3775, 3787, 3790, 3789},

				{3661, 3762, 3774, 3775, 3774},

				{3634, 3746, 3764, 3761, 3750},

				{3606, 3724, 3749, 3744, 3730},

				{3573, 3693, 3723, 3720, 3706},

				{3538, 3667, 3690, 3687, 3674},

				{3508, 3643, 3674, 3674, 3664},

				{3481, 3620, 3664, 3669, 3660},

				{3467, 3608, 3658, 3665, 3656},

				{3451, 3593, 3651, 3659, 3650},

				{3433, 3574, 3641, 3648, 3636},

				{3412, 3546, 3622, 3625, 3607},

				{3388, 3507, 3585, 3590, 3568},

				{3359, 3461, 3535, 3543, 3520},

				{3323, 3407, 3471, 3485, 3461},

				{3280, 3341, 3395, 3411, 3387},

				{3221, 3259, 3307, 3320, 3292},

				{3135, 3156, 3192, 3193, 3155},

				{3000, 3000, 3000, 3000, 3000}

	}

};



struct bms_battery_data A500KL_2050mAh_Battery_Profile = {

	.fcc				= 2050,

	.fcc_temp_lut			= &A500KL_fcc_temp,

	.fcc_sf_lut				= &A500KL_fcc_sf,

	.pc_temp_ocv_lut		= &A500KL_pc_temp_ocv,

	.rbatt_sf_lut			= &A500KL_rbatt_sf,

	.default_rbatt_mohm	= 143

};

static struct single_row_lut LG_fcc_temp = {

	.x		= {-20, 0, 25, 40, 60},

	.y		= {2030, 2030, 2030, 2030, 2030},

	.cols	= 5

};



static struct single_row_lut LG_fcc_sf = {

	.x		= {0},

	.y		= {100},

	.cols	= 1

};



static struct sf_lut LG_rbatt_sf = {

	.rows		= 30,

	.cols		= 5,

	.row_entries		= {-20, 0, 25, 40, 60},

	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 16, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1},

	.sf		= {

				{1280, 334, 100, 75, 68},

				{1234, 335, 100, 77, 69},

				{1236, 335, 100, 77, 69},

				{1192, 337, 103, 78, 70},

				{1150, 338, 106, 80, 72},

				{1127, 335, 111, 83, 74},

				{1118, 324, 120, 88, 76},

				{1119, 316, 131, 94, 81},

				{1133, 311, 130, 105, 88},

				{1156, 309, 103, 81, 72},

				{1180, 312, 100, 77, 70},

				{1212, 319, 102, 79, 72},

				{1254, 334, 104, 83, 75},

				{1312, 357, 108, 86, 79},

				{1391, 380, 113, 83, 72},

				{1495, 400, 115, 81, 71},

				{1839, 421, 118, 83, 72},

				{2326, 455, 118, 82, 72},

				{2875, 490, 120, 83, 74},

				{3300, 503, 126, 88, 76},

				{3411, 490, 127, 90, 77},

				{3467, 481, 134, 92, 78},

				{3971, 495, 143, 95, 77},

				{4681, 508, 150, 96, 81},

				{5655, 520, 151, 104, 87},

				{7006, 545, 154, 111, 93},

				{9028, 588, 154, 119, 100},

				{12202, 663, 161, 125, 106},

				{17223, 819, 182, 136, 117},

				{25932, 1457, 241, 177, 176},

	}

};



static struct pc_temp_ocv_lut LG_pc_temp_ocv = {

	.rows		= 31,

	.cols		= 5,

	.temp		= {-20, 0, 25, 40, 60},

	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 16, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},

	.ocv		= {

				{4328, 4328, 4323, 4317, 4309},

				{4204, 4243, 4252, 4250, 4246},

				{4122, 4182, 4193, 4193, 4189},

				{4051, 4126, 4138, 4138, 4134},

				{3983, 4072, 4086, 4085, 4082},

				{3925, 4019, 4037, 4037, 4033},

				{3876, 3966, 3992, 3992, 3989},

				{3835, 3918, 3952, 3952, 3949},

				{3804, 3878, 3907, 3912, 3910},

				{3776, 3843, 3856, 3858, 3858},

				{3747, 3814, 3826, 3829, 3829},

				{3718, 3791, 3805, 3807, 3807},

				{3689, 3775, 3787, 3790, 3789},

				{3661, 3762, 3774, 3775, 3774},

				{3634, 3746, 3764, 3761, 3750},

				{3606, 3724, 3749, 3744, 3730},

				{3573, 3693, 3723, 3720, 3706},

				{3538, 3667, 3690, 3687, 3674},

				{3508, 3643, 3674, 3674, 3664},

				{3481, 3620, 3664, 3669, 3660},

				{3467, 3608, 3658, 3665, 3656},

				{3451, 3593, 3651, 3659, 3650},

				{3433, 3574, 3641, 3648, 3636},

				{3412, 3546, 3622, 3625, 3607},

				{3388, 3507, 3585, 3590, 3568},

				{3359, 3461, 3535, 3543, 3520},

				{3323, 3407, 3471, 3485, 3461},

				{3280, 3341, 3395, 3411, 3387},

				{3221, 3259, 3307, 3320, 3292},

				{3135, 3156, 3192, 3193, 3155},

				{3000, 3000, 3000, 3000, 3000}

	}

};



struct bms_battery_data LG_Generic_2030mAh_Battery_Profile_99 = {

	.fcc				= 2030,

	.fcc_temp_lut			= &LG_fcc_temp,

	.fcc_sf_lut				= &LG_fcc_sf,

	.pc_temp_ocv_lut		= &LG_pc_temp_ocv,

	.rbatt_sf_lut			= &LG_rbatt_sf,

	.default_rbatt_mohm	= 143

};


#elif defined(ASUS_A600KL_PROJECT)

static struct single_row_lut A600KL_fcc_temp = {

	.x		= {-20, 0, 25, 40, 60},

	.y		= {3242, 3248, 3248, 3245, 3229},

	.cols	= 5

};



static struct single_row_lut A600KL_fcc_sf = {

	.x		= {0},

	.y		= {100},

	.cols	= 1

};



static struct sf_lut A600KL_rbatt_sf = { 

        .rows           = 28, 

        .cols           = 5,

        /* row_entries are temperature */

        .row_entries            = {-20, 0, 25, 40, 60},

        .percent        = {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1},

        .sf                     = {

{1396,304,100,82,77},

{1377,313,103,85,78},

{1359,324,109,88,80},

{1345,335,113,90,82},

{1325,349,119,94,84},

{1343,337,124,97,86},

{1360,334,129,101,89},

{1380,340,129,104,91},

{1405,344,108,88,81},

{1437,357,107,87,80},

{1480,372,111,89,81},

{1537,394,116,94,85},

{1604,423,121,97,88},

{1682,457,126,95,84},

{1777,494,129,94,82},

{1894,536,130,94,82},

{2038,596,130,94,81},

{2543,691,140,98,85},

{1948,641,142,100,85},

{2037,654,146,102,86},

{2207,676,152,103,86},

{2424,696,156,103,84},

{2690,718,156,101,84},

{3037,741,153,101,85},

{3514,768,158,105,88},

{4214,805,169,116,97},

{5455,865,193,131,103},

{8951,1027,260,204,121},

        }

};



static struct pc_temp_ocv_lut A600KL_pc_temp_ocv = {

	.rows		= 29,

	.cols		= 5,

	.temp		= {-20, 0, 25, 40, 60},

	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},

	.ocv		= {

				{4329, 4327, 4323, 4318, 4311},

				{4244, 4255, 4258, 4256, 4252},

				{4177, 4197, 4203, 4202, 4198},

				{4116, 4143, 4151, 4150, 4146},

				{4057, 4092, 4100, 4099, 4096},

				{3992, 4041, 4054, 4053, 4048},

				{3946, 3980, 4004, 4006, 4004},

				{3905, 3933, 3962, 3965, 3963},

				{3870, 3895, 3918, 3922, 3922},

				{3841, 3861, 3872, 3874, 3873},

				{3816, 3833, 3842, 3844, 3843},

				{3793, 3809, 3819, 3820, 3820},

				{3772, 3790, 3800, 3801, 3800},

				{3754, 3775, 3783, 3783, 3782},

				{3737, 3760, 3770, 3766, 3759},

				{3719, 3743, 3754, 3747, 3734},

				{3698, 3725, 3728, 3721, 3709},

				{3670, 3704, 3696, 3688, 3673},

				{3631, 3681, 3680, 3674, 3663},

				{3620, 3674, 3678, 3672, 3661},

				{3607, 3665, 3674, 3668, 3657},

				{3591, 3651, 3665, 3658, 3645},

				{3572, 3632, 3644, 3634, 3615},

				{3546, 3603, 3606, 3593, 3573},

				{3512, 3562, 3555, 3541, 3522},

				{3464, 3506, 3490, 3476, 3460},

				{3394, 3427, 3405, 3391, 3375},

				{3273, 3300, 3278, 3260, 3250},

				{3000, 3000, 3000, 3000, 3000}

	}

};





struct bms_battery_data A600KL_3200mAh_Profile = {

	.fcc				= 3200,

	.fcc_temp_lut		= &A600KL_fcc_temp,

	.fcc_sf_lut			= &A600KL_fcc_sf,

	.pc_temp_ocv_lut	= &A600KL_pc_temp_ocv,

.rbatt_sf_lut		=&A600KL_rbatt_sf,

	.default_rbatt_mohm		=114,



};


#endif
static struct single_row_lut fcc_temp = {
	.x		= {-20, 0, 25, 40, 65},
	.y		= {1492, 1492, 1493, 1483, 1502},
	.cols	= 5
};

static struct pc_temp_ocv_lut pc_temp_ocv = {
	.rows		= 29,
	.cols		= 5,
	.temp		= {-20, 0, 25, 40, 65},
	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40,
					35, 30, 25, 20, 15, 10, 9, 8, 7, 6, 5,
					4, 3, 2, 1, 0},
	.ocv		= {
				{4173, 4167, 4163, 4156, 4154},
				{4104, 4107, 4108, 4102, 4104},
				{4057, 4072, 4069, 4061, 4060},
				{3973, 4009, 4019, 4016, 4020},
				{3932, 3959, 3981, 3982, 3983},
				{3899, 3928, 3954, 3950, 3950},
				{3868, 3895, 3925, 3921, 3920},
				{3837, 3866, 3898, 3894, 3892},
				{3812, 3841, 3853, 3856, 3862},
				{3794, 3818, 3825, 3823, 3822},
				{3780, 3799, 3804, 3804, 3803},
				{3768, 3787, 3790, 3788, 3788},
				{3757, 3779, 3778, 3775, 3776},
				{3747, 3772, 3771, 3766, 3765},
				{3736, 3763, 3766, 3760, 3746},
				{3725, 3749, 3756, 3747, 3729},
				{3714, 3718, 3734, 3724, 3706},
				{3701, 3703, 3696, 3689, 3668},
				{3675, 3695, 3682, 3675, 3662},
				{3670, 3691, 3680, 3673, 3661},
				{3661, 3686, 3679, 3672, 3656},
				{3649, 3680, 3676, 3669, 3641},
				{3633, 3669, 3667, 3655, 3606},
				{3610, 3647, 3640, 3620, 3560},
				{3580, 3607, 3596, 3572, 3501},
				{3533, 3548, 3537, 3512, 3425},
				{3457, 3468, 3459, 3429, 3324},
				{3328, 3348, 3340, 3297, 3172},
				{3000, 3000, 3000, 3000, 3000}
	}
};

static struct sf_lut rbatt_sf = {
	.rows		= 29,
	.cols		= 5,
	/* row_entries are temperature */
	.row_entries	= {-20, 0, 20, 40, 65},
	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40,
					35, 30, 25, 20, 15, 10, 9, 8, 7, 6, 5,
					4, 3, 2, 1, 0},
	.sf		= {
				{357, 187, 100, 91, 91},
				{400, 208, 105, 94, 94},
				{390, 204, 106, 95, 96},
				{391, 201, 108, 98, 98},
				{391, 202, 110, 98, 100},
				{390, 200, 110, 99, 102},
				{389, 200, 110, 99, 102},
				{393, 202, 101, 93, 100},
				{407, 205, 99, 89, 94},
				{428, 208, 100, 91, 96},
				{455, 212, 102, 92, 98},
				{495, 220, 104, 93, 101},
				{561, 232, 107, 95, 102},
				{634, 245, 112, 98, 98},
				{714, 258, 114, 98, 98},
				{791, 266, 114, 97, 100},
				{871, 289, 108, 95, 97},
				{973, 340, 124, 108, 105},
				{489, 241, 109, 96, 99},
				{511, 246, 110, 96, 99},
				{534, 252, 111, 95, 98},
				{579, 263, 112, 96, 96},
				{636, 276, 111, 95, 97},
				{730, 294, 109, 96, 99},
				{868, 328, 112, 98, 104},
				{1089, 374, 119, 101, 115},
				{1559, 457, 128, 105, 213},
				{12886, 1026, 637, 422, 3269},
				{170899, 127211, 98968, 88907, 77102},
	}
};

struct bms_battery_data palladium_1500_data = {
	.fcc			= 1500,
	.fcc_temp_lut		= &fcc_temp,
	.pc_temp_ocv_lut	= &pc_temp_ocv,
	.rbatt_sf_lut		= &rbatt_sf,
	.default_rbatt_mohm	= 236,
	.rbatt_capacitive_mohm	= 50,
	.flat_ocv_threshold_uv	= 3800000,
};
