/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * Load custom textures & robot data
 */

#include <memory>
#include <string.h>
#include "gr.h"
#include "pstypes.h"
#include "piggy.h"
#include "textures.h"
#include "robot.h"
#include "weapon.h"
#include "digi.h"
#include "hash.h"
#include "u_mem.h"
#include "custom.h"
#include "physfsx.h"

#include "compiler-range_for.h"
#include "d_zip.h"
#include "partial_range.h"
#include <iterator>
#include <memory>

//#define D2TMAP_CONV // used for testing

namespace {

struct snd_info
{
	std::size_t length;
	digi_sound::allocated_data data;
};

}

struct DiskBitmapHeader2 
{
	char name[8];
	ubyte dflags;
	ubyte width;
	ubyte height;
	ubyte hi_wh;
	ubyte flags;
	ubyte avg_color;
	int offset;
} __pack__;

struct DiskBitmapHeader
{
	char name[8];
	ubyte dflags;
	ubyte width;
	ubyte height;
	ubyte flags;
	ubyte avg_color;
	int offset;
} __pack__;

struct DiskSoundHeader
{
	char name[8];
	int length;
	int data_length;
	int offset;
} __pack__;

namespace {

struct custom_info
{
	int offset;
	int repl_idx; // 0x80000000 -> sound, -1 -> n/a
	unsigned int flags;
	int width, height;
};

struct bitmap_original
{
	/* Reuse the type grs_bitmap since it covers most of what is needed.
	 * However, instances of bitmap_original are not suitable for direct
	 * use in places where a grs_bitmap is expected.
	 */
	grs_bitmap b;
};

constexpr std::integral_constant<uint8_t, 0x80> BM_FLAG_CUSTOMIZED{};

}

static std::array<bitmap_original, MAX_BITMAP_FILES> BitmapOriginal;
static std::array<snd_info, MAX_SOUND_FILES> SoundOriginal;

static int load_pig1(PHYSFS_File *f, unsigned num_bitmaps, unsigned num_sounds, unsigned &num_custom, std::unique_ptr<custom_info[]> &ci)
{
	int data_ofs;
	int i;
	DiskBitmapHeader bmh;
	DiskSoundHeader sndh;
	char name[15];

	num_custom = 0;

	if (num_bitmaps <= MAX_BITMAP_FILES) // <v1.4 pig?
	{
		PHYSFS_seek(f, 8);
		data_ofs = 8;
	}
	else if (num_bitmaps > 0 && num_bitmaps < PHYSFS_fileLength(f)) // >=v1.4 pig?
	{
		PHYSFS_seek(f, num_bitmaps);
		data_ofs = num_bitmaps + 8;
		num_bitmaps = PHYSFSX_readInt(f);
		num_sounds = PHYSFSX_readInt(f);
	}
	else
		return -1; // invalid pig file

	if (num_bitmaps >= MAX_BITMAP_FILES ||
		num_sounds >= MAX_SOUND_FILES)
		return -1; // invalid pig file
	ci = std::make_unique<custom_info[]>(num_bitmaps + num_sounds);
	custom_info *cip = ci.get();
	data_ofs += num_bitmaps * sizeof(DiskBitmapHeader) + num_sounds * sizeof(DiskSoundHeader);
	i = num_bitmaps;

	while (i--)
	{
		if (PHYSFS_read(f, &bmh, sizeof(DiskBitmapHeader), 1) < 1)
		{
			return -1;
		}

		snprintf(name, sizeof(name), "%.8s%c%d", bmh.name, (bmh.dflags & DBM_FLAG_ABM) ? '#' : 0, bmh.dflags & 63);

		cip->offset = bmh.offset + data_ofs;
		cip->repl_idx = hashtable_search(&AllBitmapsNames, name);
		cip->flags = bmh.flags & (BM_FLAG_TRANSPARENT | BM_FLAG_SUPER_TRANSPARENT | BM_FLAG_NO_LIGHTING | BM_FLAG_RLE);
		cip->width = bmh.width + ((bmh.dflags & DBM_FLAG_LARGE) ? 256 : 0);
		cip->height = bmh.height;
		cip++;
	}

	i = num_sounds;

	while (i--)
	{
		if (PHYSFS_read(f, &sndh, sizeof(DiskSoundHeader), 1) < 1)
		{
			return -1;
		}

		memcpy(name, sndh.name, 8);
		name[8] = 0;
		cip->offset = sndh.offset + data_ofs;
		cip->repl_idx = hashtable_search(&AllDigiSndNames, name) | 0x80000000;
		cip->width = sndh.length;
		cip++;
	}

	num_custom = num_bitmaps + num_sounds;

	return 0;
}

static int load_pog(PHYSFS_File *f, int pog_sig, int pog_ver, unsigned &num_custom, std::unique_ptr<custom_info[]> &ci)
{
	int data_ofs;
	int num_bitmaps;
	int no_repl = 0;
	DiskBitmapHeader2 bmh;

#ifdef D2TMAP_CONV
	int x, j, N_d2tmap;
	int *d2tmap = NULL;
	PHYSFS_File *f2 = NULL;
	if ((f2 = PHYSFSX_openReadBuffered("d2tmap.bin")))
	{
		N_d2tmap = PHYSFSX_readInt(f2);
		if ((d2tmap = d_malloc(N_d2tmap * sizeof(d2tmap[0]))))
			for (i = 0; i < N_d2tmap; i++)
				d2tmap[i] = PHYSFSX_readShort(f2);
		PHYSFS_close(f2);
	}
#endif

	num_custom = 0;

	if (pog_sig == 0x47495050 && pog_ver == 2) /* PPIG */
		no_repl = 1;
	else if (pog_sig != 0x474f5044 || pog_ver != 1) /* DPOG */
		return -1; // no pig2/pog file/unknown version

	num_bitmaps = PHYSFSX_readInt(f);
	ci = std::make_unique<custom_info[]>(num_bitmaps);
	custom_info *cip = ci.get();
	data_ofs = 12 + num_bitmaps * sizeof(DiskBitmapHeader2);

	if (!no_repl)
	{
		for (int i = num_bitmaps; i--;)
			(cip++)->repl_idx = PHYSFSX_readShort(f);

		cip = ci.get();
		data_ofs += num_bitmaps * 2;
	}

#ifdef D2TMAP_CONV
	if (d2tmap)
		for (i = 0; i < num_bitmaps; i++)
		{
			x = cip[i].repl_idx;
			cip[i].repl_idx = -1;

			for (j = 0; j < N_d2tmap; j++)
				if (x == d2tmap[j])
				{
					cip[i].repl_idx = Textures[j % NumTextures].index;
					break;
				}
		}
#endif
	for (int i = num_bitmaps; i--;)
	{
		if (PHYSFS_read(f, &bmh, sizeof(DiskBitmapHeader2), 1) < 1)
		{
			return -1;
		}

		cip->offset = bmh.offset + data_ofs;
		cip->flags = bmh.flags & (BM_FLAG_TRANSPARENT | BM_FLAG_SUPER_TRANSPARENT | BM_FLAG_NO_LIGHTING | BM_FLAG_RLE);
		cip->width = bmh.width + ((bmh.hi_wh & 15) << 8);
		cip->height = bmh.height + ((bmh.hi_wh >> 4) << 8);
		cip++;
	}

	num_custom = num_bitmaps;

	return 0;
}

// load custom textures/sounds from pog/pig file
// returns 0 if ok, <0 on error
static int load_pigpog(const d_fname &pogname)
{
	unsigned num_custom;
	grs_bitmap *bmp;
	auto f = PHYSFSX_openReadBuffered(pogname).first;
	int i, j, rc = -1;
	unsigned int x = 0;

	if (!f)
		return -1; // pog file doesn't exist

	i = PHYSFSX_readInt(f);
	x = PHYSFSX_readInt(f);

	std::unique_ptr<custom_info[]> ci;
	if (load_pog(f, i, x, num_custom, ci) && load_pig1(f, i, x, num_custom, ci))
	{
		return rc;
	}

	custom_info *cip = ci.get();
	i = num_custom;

	while (i--)
	{
		x = cip->repl_idx;
		if (cip->repl_idx >= 0)
		{
			PHYSFS_seek(f, cip->offset);

			if ( cip->flags & BM_FLAG_RLE )
				j = PHYSFSX_readInt(f);
			else
				j = cip->width * cip->height;

			uint8_t *p;
			if (!MALLOC(p, ubyte, j))
			{
				return rc;
			}

			bmp = &GameBitmaps[x];

			auto &bmo = BitmapOriginal[x].b;
			if (bmo.get_flag_mask(BM_FLAG_CUSTOMIZED)) // already customized?
				/* If the bitmap was previously customized, and another
				 * customization is loaded on top of it, discard the
				 * first customization.  Retain the stock bitmap in the
				 * backup data structure.
				 */
				gr_free_bitmap_data(*bmp);
			else
			{
				/* If the bitmap was not previously customized, then
				 * copy it to a backup data structure before replacing
				 * it with the custom version.
				 */
				// save original bitmap info
				bmo = *bmp;
				bmo.add_flags(BM_FLAG_CUSTOMIZED);
				if (GameBitmapOffset[x] != pig_bitmap_offset::None) // from pig?
				{
					/* Borrow the data field to store the offset within
					 * the pig file, which is not a pointer.  Attempts
					 * to dereference bm_data will almost certainly
					 * crash.
					 */
					bmo.add_flags(BM_FLAG_PAGED_OUT);
					bmo.bm_data = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(GameBitmapOffset[x]));
				}
			}

			GameBitmapOffset[x] = pig_bitmap_offset::None; // not in pig
			*bmp = {};
			gr_init_bitmap(*bmp, bm_mode::linear, 0, 0, cip->width, cip->height, cip->width, p);
			gr_set_bitmap_flags(*bmp, cip->flags & 255);
#if !DXX_USE_OGL
			bmp->avg_color = cip->flags >> 8;
#endif

			if ( cip->flags & BM_FLAG_RLE )
			{
				int *ip = reinterpret_cast<int *>(p);
				*ip = j;
				p += 4;
				j -= 4;
			}

			if (PHYSFS_read(f, p, 1, j) < 1)
			{
				return rc;
			}

		}
		else if ((cip->repl_idx + 1) < 0)
		{
			PHYSFS_seek(f, cip->offset);
			auto snd = &GameSounds[x & 0x7fffffff];

			j = cip->width;
			auto p = std::make_unique<uint8_t[]>(j);
			if (SoundOriginal[x & 0x7fffffff].length & 0x80000000)  // already customized?
			{
			}
			else
			{
				SoundOriginal[x & 0x7fffffff].length = snd->length | 0x80000000;
				SoundOriginal[x & 0x7fffffff].data = std::move(snd->data);
			}

				snd->length = j;
			snd->data = digi_sound::allocated_data{std::move(p), game_sound_offset{}};

			if (PHYSFS_read(f, p.get(), j, 1) < 1)
			{
				return rc;
			}
		}
		cip++;
	}
	rc = 0;
	return rc;
}

static int read_d2_robot_info(PHYSFS_File *fp, robot_info &ri)
{
	int j;

	ri.model_num = build_polygon_model_index_from_untrusted(PHYSFSX_readInt(fp));

	for (auto &j : ri.gun_points)
		PHYSFSX_readVector(fp, j);
	for (auto &j : ri.gun_submodels)
		j = PHYSFSX_readByte(fp);
	ri.exp1_vclip_num = PHYSFSX_readShort(fp);
	ri.exp1_sound_num = PHYSFSX_readShort(fp);
	ri.exp2_vclip_num = PHYSFSX_readShort(fp);
	ri.exp2_sound_num = PHYSFSX_readShort(fp);
	const auto weapon_type = PHYSFSX_readByte(fp);
	ri.weapon_type = weapon_type < N_weapon_types ? static_cast<weapon_id_type>(weapon_type) : weapon_id_type::LASER_ID_L1;
	/*ri.weapon_type2 =*/ PHYSFSX_readByte(fp);
	ri.n_guns = PHYSFSX_readByte(fp);
	ri.contains_id = PHYSFSX_readByte(fp);
	ri.contains_count = PHYSFSX_readByte(fp);
	ri.contains_prob = PHYSFSX_readByte(fp);
	ri.contains_type = PHYSFSX_readByte(fp);
	/*ri.kamikaze =*/ PHYSFSX_readByte(fp);
	ri.score_value = PHYSFSX_readShort(fp);
	/*ri.badass =*/ PHYSFSX_readByte(fp);
	/*ri.energy_drain =*/ PHYSFSX_readByte(fp);
	ri.lighting = PHYSFSX_readFix(fp);
	ri.strength = PHYSFSX_readFix(fp);
	ri.mass = PHYSFSX_readFix(fp);
	ri.drag = PHYSFSX_readFix(fp);
	for (auto &j : ri.field_of_view)
		j = PHYSFSX_readFix(fp);
	for (auto &j : ri.firing_wait)
		j = PHYSFSX_readFix(fp);
	for (j = 0; j < NDL; j++)
		/*ri.firing_wait2[j] =*/ PHYSFSX_readFix(fp);
	for (auto &j : ri.turn_time)
		j = PHYSFSX_readFix(fp);
#if 0 // not used in d1, removed in d2
	for (j = 0; j < NDL; j++)
		ri.fire_power[j] = PHYSFSX_readFix(fp);
	for (j = 0; j < NDL; j++)
		ri.shield[j] = PHYSFSX_readFix(fp);
#endif
	for (auto &j : ri.max_speed)
		j = PHYSFSX_readFix(fp);
	for (auto &j : ri.circle_distance)
		j = PHYSFSX_readFix(fp);
	for (auto &j : ri.rapidfire_count)
		j = PHYSFSX_readByte(fp);
	for (auto &j : ri.evade_speed)
		j = PHYSFSX_readByte(fp);
	ri.cloak_type = PHYSFSX_readByte(fp);
	ri.attack_type = PHYSFSX_readByte(fp);
	ri.see_sound = PHYSFSX_readByte(fp);
	ri.attack_sound = PHYSFSX_readByte(fp);
	ri.claw_sound = PHYSFSX_readByte(fp);
	/*ri.taunt_sound =*/ PHYSFSX_readByte(fp);
	ri.boss_flag = PHYSFSX_readByte(fp);
	/*ri.companion =*/ PHYSFSX_readByte(fp);
	/*ri.smart_blobs =*/ PHYSFSX_readByte(fp);
	/*ri.energy_blobs =*/ PHYSFSX_readByte(fp);
	/*ri.thief =*/ PHYSFSX_readByte(fp);
	/*ri.pursuit =*/ PHYSFSX_readByte(fp);
	/*ri.lightcast =*/ PHYSFSX_readByte(fp);
	/*ri.death_roll =*/ PHYSFSX_readByte(fp);
	/*ri.flags =*/ PHYSFSX_readByte(fp);
	/*ri.pad[0] =*/ PHYSFSX_readByte(fp);
	/*ri.pad[1] =*/ PHYSFSX_readByte(fp);
	/*ri.pad[2] =*/ PHYSFSX_readByte(fp);
	/*ri.deathroll_sound =*/ PHYSFSX_readByte(fp);
	/*ri.glow =*/ PHYSFSX_readByte(fp);
	/*ri.behavior =*/ PHYSFSX_readByte(fp);
	/*ri.aim =*/ PHYSFSX_readByte(fp);

	for (auto &j : ri.anim_states)
	{
		for (auto &k : j)
		{
			k.n_joints = PHYSFSX_readShort(fp);
			k.offset = PHYSFSX_readShort(fp);
		}
	}
	ri.always_0xabcd = PHYSFSX_readInt(fp);

	return 1;
}

namespace dsx {

static void load_hxm(const d_fname &hxmname)
{
	auto &Robot_joints = LevelSharedRobotJointState.Robot_joints;
	int i;
	auto f = PHYSFSX_openReadBuffered(hxmname).first;
	int n_items;

	if (!f)
		return; // hxm file doesn't exist

	if (PHYSFSX_readInt(f) != 0x21584d48) /* HMX! */
	{
		// invalid hxm file
		return;
	}

	if (PHYSFSX_readInt(f) != 1)
	{
		// unknown version
		return;
	}

	// read robot info
	if ((n_items = PHYSFSX_readInt(f)) != 0)
	{
		auto &Robot_info = LevelSharedRobotInfoState.Robot_info;
		for (i = 0; i < n_items; i++)
		{
			const unsigned repl_num = PHYSFSX_readInt(f);
			if (repl_num >= MAX_ROBOT_TYPES)
			{
				PHYSFSX_fseek(f, 480, SEEK_CUR); /* sizeof d2_robot_info */
			}
			else
			{
				if (!(read_d2_robot_info(f, Robot_info[repl_num])))
				{
					return;
				}
			}
		}
	}

	// read joint positions
	if ((n_items = PHYSFSX_readInt(f)) != 0)
	{
		for (i = 0; i < n_items; i++)
		{
			const unsigned repl_num = PHYSFSX_readInt(f);
			if (repl_num >= MAX_ROBOT_JOINTS)
				PHYSFSX_fseek(f, sizeof(jointpos), SEEK_CUR);
			else
			{
				jointpos_read(f, Robot_joints[repl_num]);
			}
		}
	}

	// read polygon models
	if ((n_items = PHYSFSX_readInt(f)) != 0)
	{
		for (i = 0; i < n_items; i++)
		{
			polymodel *pm;
			if (const auto repl_num = build_polygon_model_index_from_untrusted(PHYSFSX_readInt(f)); repl_num == polygon_model_index::None)
			{
				PHYSFSX_readInt(f); // skip n_models
				PHYSFSX_fseek(f, 734 - 8 + PHYSFSX_readInt(f) + 8, SEEK_CUR);
			}
			else
			{
				auto &Polygon_models = LevelSharedPolygonModelState.Polygon_models;
				pm = &Polygon_models[repl_num];
				polymodel_read(*pm, f);
				const auto model_data_size = pm->model_data_size;
				pm->model_data = std::make_unique<uint8_t[]>(model_data_size);
				if (PHYSFS_read(f, pm->model_data, model_data_size, 1) < 1)
				{
					pm->model_data.reset();
					return;
				}

				Dying_modelnums[repl_num] = build_polygon_model_index_from_untrusted(PHYSFSX_readInt(f));
				Dead_modelnums[repl_num] = build_polygon_model_index_from_untrusted(PHYSFSX_readInt(f));
			}
		}
	}

	// read object bitmaps
	if ((n_items = PHYSFSX_readInt(f)) != 0)
	{
		for (i = 0; i < n_items; i++)
		{
			const auto oi = static_cast<object_bitmap_index>(PHYSFSX_readInt(f));
			auto v = PHYSFSX_readShort(f);
			if (ObjBitmaps.valid_index(oi))
				ObjBitmaps[oi].index = v;
		}
	}
}

}

// undo customized items
static void custom_remove()
{
	int i;

	for (auto &&[gbo, bmo, bmp] : zip(GameBitmapOffset, BitmapOriginal, GameBitmaps))
		if (bmo.b.get_flag_mask(BM_FLAG_CUSTOMIZED))
		{
			gr_free_bitmap_data(bmp);
			bmp = bmo.b;
			bmo.b.clear_flags();

			if (bmp.get_flag_mask(BM_FLAG_PAGED_OUT))
			{
				/* If the bitmap was unloaded before it was customized,
				 * then restore GameBitmapOffset so that the stock data
				 * can be reloaded when it is next needed.
				 */
				/* Casting this pointer into a smaller integer is legal
				 * because the value in bm_data is not a pointer.  It is
				 * a copy of the value that GameBitmapOffset had when
				 * the bitmap_original was initialized.
				 */
				gbo = static_cast<pig_bitmap_offset>(reinterpret_cast<uintptr_t>(bmo.b.bm_data));
				gr_set_bitmap_data(bmp, nullptr);
			}
			else
			{
				/* Otherwise, the bitmap was loaded before it was
				 * overwritten, so the copy restored from the backup is
				 * ready for use.
				 */
				bmp.set_flags(bmp.get_flags() & ~BM_FLAG_CUSTOMIZED);
			}
		}
	for (i = 0; i < MAX_SOUND_FILES; i++)
		if (SoundOriginal[i].length & 0x80000000)
		{
			GameSounds[i].data = std::move(SoundOriginal[i].data);
			GameSounds[i].length = SoundOriginal[i].length & 0x7fffffff;
			SoundOriginal[i].length = 0;
		}
}

void load_custom_data(const d_fname &level_name)
{
	custom_remove();
	d_fname custom_file;
	using std::begin;
	using std::end;
	using std::copy;
	using std::next;
	auto bl = begin(level_name);
	auto bc = begin(custom_file);
	auto &pg1 = ".pg1";
	copy(bl, next(bl, custom_file.size() - sizeof(pg1)), bc);
	auto o = std::find(bc, next(bc, custom_file.size() - sizeof(pg1)), '.');
	copy(begin(pg1), end(pg1), o);
	load_pigpog(custom_file);
	auto &dtx = "dtx";
	++o;
	copy(begin(dtx), end(dtx), o);
	load_pigpog(custom_file);
	auto &hx1 = "hx1";
	copy(begin(hx1), end(hx1), o);
	load_hxm(custom_file);
}

void custom_close()
{
	custom_remove();
}
