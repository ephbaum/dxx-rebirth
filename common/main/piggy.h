/*
 * Portions of this file are copyright Rebirth contributors and licensed as
 * described in COPYING.txt.
 * Portions of this file are copyright Parallax Software and licensed
 * according to the Parallax license below.
 * See COPYING.txt for license details.

THE COMPUTER CODE CONTAINED HEREIN IS THE SOLE PROPERTY OF PARALLAX
SOFTWARE CORPORATION ("PARALLAX").  PARALLAX, IN DISTRIBUTING THE CODE TO
END-USERS, AND SUBJECT TO ALL OF THE TERMS AND CONDITIONS HEREIN, GRANTS A
ROYALTY-FREE, PERPETUAL LICENSE TO SUCH END-USERS FOR USE BY SUCH END-USERS
IN USING, DISPLAYING,  AND CREATING DERIVATIVE WORKS THEREOF, SO LONG AS
SUCH USE, DISPLAY OR CREATION IS FOR NON-COMMERCIAL, ROYALTY OR REVENUE
FREE PURPOSES.  IN NO EVENT SHALL THE END-USER USE THE COMPUTER CODE
CONTAINED HEREIN FOR REVENUE-BEARING PURPOSES.  THE END-USER UNDERSTANDS
AND AGREES TO THE TERMS HEREIN AND ACCEPTS THE SAME BY USE OF THIS FILE.
COPYRIGHT 1993-1999 PARALLAX SOFTWARE CORPORATION.  ALL RIGHTS RESERVED.
*/

/*
 *
 * Interface to piggy functions.
 *
 */

#pragma once

#include "dsx-ns.h"
#include "fwd-inferno.h"
#include "fwd-piggy.h"
#include "fwd-robot.h"
#include "gr.h"

#if defined(DXX_BUILD_DESCENT_II)
namespace dsx {
struct alias
{
	char alias_name[FILENAME_LEN];
	char file_name[FILENAME_LEN];
};
}
#endif

// an index into the bitmap collection of the piggy file
struct bitmap_index
{
	ushort index;
	constexpr bool operator==(const bitmap_index &) const = default;
};
#define DEFINE_BITMAP_SERIAL_UDT() DEFINE_SERIAL_UDT_TO_MESSAGE(bitmap_index, b, (b.index))

struct BitmapFile
{
	std::array<char, 13> name;
};

#ifdef dsx
namespace dsx {

#  define  PIGGY_PAGE_IN(bmp) _piggy_page_in(GameBitmaps, bmp)
static inline void _piggy_page_in(GameBitmaps_array &GameBitmaps, bitmap_index bmp)
{
	if (GameBitmaps[bmp.index].get_flag_mask(BM_FLAG_PAGED_OUT))
        piggy_bitmap_page_in(GameBitmaps, bmp);
}

int properties_init(d_level_shared_robot_info_state &LevelSharedRobotInfoState);

#if defined(DXX_BUILD_DESCENT_II)
enum class pig_hamfile_version : uint8_t
{
	_0,
	_3 = 3,
};

int read_hamfile(d_level_shared_robot_info_state &LevelSharedRobotInfoState);
#endif
}

#endif
