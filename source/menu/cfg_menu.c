#include "global.h"

#include "3ds/services/fs.h"

#include <string.h>

#define SETTINGS_VERSION 1
#define SETTINGS_FILE_NAME "ntr_settings.ini"

static FS_Archive sdmc_archive;

static s32 open_sdmc_archive(void)
{
	s32 ret = FSUSER_OpenArchive(&sdmc_archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, NULL));

	if (ret)
		showDbg("Open sdmc archive failed: %"PRIx32, ret);

	return ret;
}

static void close_sdmc_archive(void)
{
	FSUSER_CloseArchive(sdmc_archive);
	sdmc_archive = 0;
}

struct settings_u32_key_t {
	const char *name;
	u32 offset;
};
#define SK(sname, name, field) { name, offsetof(sname, field) }
#define SK_S { NULL, 0 }
#define SK_RP(field) SK(RP_CONFIG, #field, field)
static const struct settings_u32_key_t rp_u32_keys[] = {
	SK_RP(mode),
	SK_RP(quality),
	SK_RP(qos),
	SK_RP(dstPort),
	SK_RP(dstAddr),
	SK_RP(coreCount),
	SK_RP(threadPriority),
	SK_RP(separateScreenConfig),
	SK_RP(fullWidth),
	SK_RP(audioEnable),
	SK_S,
};
#undef SK_RP
#define SK_RPS(field) SK(RP_SCREEN_CONFIG, #field, field)
static const struct settings_u32_key_t rps_u32_keys[] = {
	SK_RPS(chromaSs),
	SK_RPS(downsample),
	SK_RPS(dither),
	SK_RPS(fpsLimit),
	SK_RPS(skipDupFrame),
	SK_S,
};
#undef SK_RPS
#define SK_PLG(field) SK(PLGLOADER_EX_INFO, #field, field)
static const struct settings_u32_key_t plg_u32_keys[] = {
	SK_PLG(noPlugins),
	SK_PLG(CTRPFCompat),
	SK_PLG(remotePlayBoost),
	SK_PLG(overlayStats),
	SK_PLG(noLoaderMem),
	SK_S,
};
#undef SK_PLG
#define SK_M(field) SK(struct ntr_options_misc_stage, #field, field)
static const struct settings_u32_key_t misc_u32_keys[] = {
	SK_M(menu_hotkey),
	SK_M(qtm_disable),
	SK_S,
};
#undef SK_M
#undef SK

static s32 appendFile(Handle file, u32 *offset, const char *str)
{
	u32 written;
	u32 size = strlen(str);
	s32 ret = FSFILE_Write(file, &written, *offset, str, size, 0);
	if (ret) {
		showDbg("Failed to write \"%s\" to file at offset %"PRIu32, str, *offset);
		return ret;
	}
	if (size != written) {
		showDbg("Failed to fully write \"%s\" to file at offset %"PRIu32, str, *offset);
		return -1;
	}

	*offset += written;
	return 0;
}

static s32 ntrSaveOptionsToFile(Handle file)
{
	struct ntr_options_stage options_stage;

	options_stage.rp.rp = *rpConfig;
	options_stage.plg.plg = *plgLoaderEx;
	ntr_options_misc_save(&options_stage.misc);

	u32 offset = 0;
	char buf[LOCAL_OPT_TEXT_BUF_SIZE];
#define APPEND_OR_RET(fmt, ...) do { \
	xsnprintf(buf, LOCAL_OPT_TEXT_BUF_SIZE, fmt, ## __VA_ARGS__); \
	s32 ret = appendFile(file, &offset, buf); \
	if (ret) \
		return ret; \
} while (0)

	APPEND_OR_RET("version=%d\n", SETTINGS_VERSION);

	APPEND_OR_RET("\n[remote_play]\n");
#define APPEND_KEYS(keys, s) do { \
	for (int i = 0; (keys)[i].name; ++i) { \
		APPEND_OR_RET("%s=%"PRIu32"\n", (keys)[i].name, *(u32 *)((char *)(s) + (keys)[i].offset)); \
	} \
} while (0)
	APPEND_KEYS(rp_u32_keys, &options_stage.rp.rp);

	APPEND_OR_RET("\n[remote_play_screen.top]\n");
	APPEND_KEYS(rps_u32_keys, &options_stage.rp.rp.screens[RP_SCREEN_TOP]);

	APPEND_OR_RET("\n[remote_play_screen.bottom]\n");
	APPEND_KEYS(rps_u32_keys, &options_stage.rp.rp.screens[RP_SCREEN_BOT]);

	APPEND_OR_RET("\n[plg_loader]\n");
	APPEND_KEYS(plg_u32_keys, &options_stage.plg.plg);

	APPEND_OR_RET("\n[misc]\n");
	APPEND_KEYS(misc_u32_keys, &options_stage.misc);

#undef APPEND_KEYS
#undef APPEND_OR_RET
	return 0;
}

s32 ntrMenuSaveOptions(void)
{
	s32 ret = open_sdmc_archive();
	if (ret)
		return ret;

	char file_name[PATH_MAX];
	ret = strnjoin(file_name, PATH_MAX, ntrConfig->ntrFilePath, SETTINGS_FILE_NAME);
	if (ret != 0) {
		showDbg("Settings file name too long: \"%s%s\"", ntrConfig->ntrFilePath, SETTINGS_FILE_NAME);
		goto final;
	}
	FS_Path file_path = fsMakePath(PATH_ASCII, file_name);

	const char *const stage_suffix = ".sav";
	char stage_file_name[PATH_MAX];
	ret = strnjoin(stage_file_name, PATH_MAX, file_name, stage_suffix);
	if (ret != 0) {
		showDbg("Settings staging file name too long: \"%s%s\"", file_name, stage_suffix);
		goto final;
	}
	FS_Path stage_file_path = fsMakePath(PATH_ASCII, stage_file_name);

	FSUSER_DeleteFile(sdmc_archive, stage_file_path);

	Handle file;
	ret = FSUSER_OpenFile(&file, sdmc_archive, stage_file_path, FS_OPEN_WRITE | FS_OPEN_CREATE, 0);
	if (ret) {
		showDbg("Settings file \"%s\" cannot be opened: %"PRIx32, stage_file_name, ret);
		goto final;
	}

	ret = ntrSaveOptionsToFile(file);
	FSFILE_Close(file);
	file = 0;

	if (ret)
		goto final;

	FSUSER_DeleteFile(sdmc_archive, file_path);
	ret = FSUSER_RenameFile(sdmc_archive, stage_file_path, sdmc_archive, file_path);
	if (ret) {
		showDbg("Failed to rename \"%s\" to file \"%s\": %"PRIx32, stage_file_name, file_name, ret);
		goto final;
	}

	showDbg("Settings saved to file \"%s\"", file_name);

final:
	close_sdmc_archive();
	return 0;
}

static char *find_chars_in_str(char *str, const char *chars)
{
	int chars_n = strlen(chars);
	while (*str) {
		for (int i = 0; i < chars_n; ++i) {
			if (*str == chars[i])
				return str;
		}
		++str;
	}
	return str;
}

static char *find_not_chars_in_str(char *str, const char *not_chars)
{
	int chars_n = strlen(not_chars);
	while (*str) {
		for (int i = 0; i < chars_n; ++i) {
			if (*str != not_chars[i])
				return str;
		}
		++str;
	}
	return str;
}

static s32 strToU32(const char *str, u32 *num)
{
	u64 out = 0;
	while (*str) {
		if (*str >= '0' && *str <= '9') {
			out *= 10;
			out += *str - '0';

			if (out > UINT32_MAX)
				return -2;
		} else {
			return -1;
		}
		++str;
	}

	*num = out;
	return 0;
}

static s32 ntrLoadOptionsFromFile(Handle file, struct ntr_options_stage *options)
{
	u32 offset = 0;

	char buf[LOCAL_OPT_TEXT_BUF_SIZE + 1];
	u32 bytes_read;

	enum {
		SECTION_NONE,
		SECTION_RP,
		SECTION_RPS_T,
		SECTION_RPS_B,
		SECTION_PLG,
		SECTION_MISC,
		SECTION_UNKNOWN,
	} section = SECTION_NONE;

	while (1) {
		s32 ret = FSFILE_Read(file, &bytes_read, offset, buf, LOCAL_OPT_TEXT_BUF_SIZE);
		if (ret) {
			showDbg("Failed to read file at offset %"PRIu32, offset);
			return ret;
		}

		if (bytes_read == 0)
			break;

		buf[bytes_read] = 0;

		const char *chars_newline = "\n";
		char *const c_newline = find_chars_in_str(buf, chars_newline);

		if (bytes_read == LOCAL_OPT_TEXT_BUF_SIZE && !*c_newline) {
			char b;
			u32 b_read;
			s32 ret = FSFILE_Read(file, &b_read, offset + bytes_read, &b, 1);
			if (ret) {
				showDbg("Failed to read eof at offset %"PRIu32, offset + bytes_read);
				return ret;
			}
			if (b_read != 0) {
				showDbg("Failed to find newline at offset %"PRIu32, offset + bytes_read);
				return -1;
			}
		}

		*c_newline = 0;

		const char *chars_whitespace = " \t\r";
		char *trim_0 = find_not_chars_in_str(buf, chars_whitespace);

		if (*trim_0 == ';' || *trim_0 == '#')
			goto next;

		if (*trim_0 == '[') {
			char *sec_name = trim_0 + 1;
			char *sec_name_end = find_chars_in_str(sec_name, "]");
			if (!*sec_name_end) {
				showDbg("Malformed section name \"%s\" at offset %"PRIu32, sec_name, offset + (sec_name_end - buf));
				goto next;
			}

			sec_name = find_not_chars_in_str(sec_name, chars_whitespace);
			char *sec_name_w_end = find_chars_in_str(sec_name, chars_whitespace);
			if (sec_name_w_end < sec_name_end)
				sec_name_end = sec_name_w_end;

			*sec_name_end = 0;

			if (!strcmp(sec_name, "remote_play"))
				section = SECTION_RP;
			else if (!strcmp(sec_name, "remote_play_screen.top"))
				section = SECTION_RPS_T;
			else if (!strcmp(sec_name, "remote_play_screen.bottom"))
				section = SECTION_RPS_B;
			else if (!strcmp(sec_name, "plg_loader"))
				section = SECTION_PLG;
			else if (!strcmp(sec_name, "misc"))
				section = SECTION_MISC;
			else {
				showDbg("Unknown section name \"%s\" at offset %"PRIu32, sec_name, offset + (sec_name - buf));
				section = SECTION_UNKNOWN;
			}

			goto next;
		}

		char *eq = find_chars_in_str(trim_0, "=");
		if (!*eq) {
			if (eq != trim_0)
				showDbg("Missing k=v \"%s\" at offset %"PRIu32, trim_0, offset + (eq - buf));
			goto next;
		}

		char *k_name = trim_0;
		char *k_name_end = find_chars_in_str(k_name, chars_whitespace);
		if (eq < k_name_end)
			k_name_end = eq;
		if (k_name == k_name_end) {
			showDbg("Missing key at offset %"PRIu32, offset + (k_name_end - buf));
			goto next;
		}
		*k_name_end = 0;

		char *v_num = find_not_chars_in_str(eq + 1, chars_whitespace);
		if (!*v_num) {
			showDbg("Missing value at offset %"PRIu32, offset + (v_num - buf));
			goto next;
		}

		*c_newline = '\n';
		char *v_num_end = find_chars_in_str(v_num + 1, chars_whitespace);
		*c_newline = 0;
		if (v_num == v_num_end) {
			showDbg("Missing value at offset %"PRIu32, offset + (v_num_end - buf));
			goto next;
		}
		*v_num_end = 0;

		u32 version = 0;
		const struct settings_u32_key_t version_keys[] = { { "version", 0, }, SK_S };
		void *stage;
		const struct settings_u32_key_t *keys;
		switch (section) {
			case SECTION_NONE:
				stage = &version;
				keys = version_keys;
				break;
			case SECTION_RP:
				stage = &options->rp.rp;
				keys = rp_u32_keys;
				break;
			case SECTION_RPS_T:
				stage = &options->rp.rp.screens[RP_SCREEN_TOP];
				keys = rps_u32_keys;
				break;
			case SECTION_RPS_B:
				stage = &options->rp.rp.screens[RP_SCREEN_BOT];
				keys = rps_u32_keys;
				break;
			case SECTION_PLG:
				stage = &options->plg.plg;
				keys = plg_u32_keys;
				break;
			case SECTION_MISC:
				stage = &options->misc;
				keys = misc_u32_keys;
				break;
			case SECTION_UNKNOWN:
				stage = NULL;
				keys = NULL;
				break;
				break;
		}

		if (!stage || !keys)
			goto next;

		for (int i = 0; keys[i].name; ++i) {
			if (!strcmp(k_name, keys[i].name)) {
				u32 num;
				s32 ret = strToU32(v_num, &num);
				if (ret) {
					showDbg("Malformed value \"%s\" at offset %"PRIu32, v_num, offset + (v_num - buf));
					goto next;
				}

				*(u32 *)((char *)stage + keys[i].offset) = num;

				goto next;
			}
		}
		showDbg("Unknown key \"%s\" at offset %"PRIu32, k_name, offset + (k_name - buf));

next:
		offset += c_newline + 1 - buf;
	}

	return 0;
}

static s32 ntrApplyOptions(struct ntr_options_stage *options)
{
	ntr_options_misc_load(&options->misc);
	ntr_options_plg_load(&options->plg);
	ntr_options_rp_load(&options->rp);
	return 0;
}

s32 ntrMenuLoadOptions(bool boot)
{
	s32 ret = open_sdmc_archive();
	if (ret)
		return ret;

	char file_name[PATH_MAX];
	ret = strnjoin(file_name, PATH_MAX, ntrConfig->ntrFilePath, SETTINGS_FILE_NAME);
	if (ret != 0) {
		showDbg("Settings file name too long: \"%s%s\"", ntrConfig->ntrFilePath, SETTINGS_FILE_NAME);
		goto final;
	}
	FS_Path file_path = fsMakePath(PATH_ASCII, file_name);

	Handle file;
	ret = FSUSER_OpenFile(&file, sdmc_archive, file_path, FS_OPEN_READ, 0);
	if (ret) {
		if (!boot)
			showDbg("Settings file \"%s\" cannot be opened: %"PRIx32, file_name, ret);
		goto final;
	}

	struct ntr_options_stage options = { 0 };
	ret = ntrLoadOptionsFromFile(file, &options);
	FSFILE_Close(file);
	file = 0;

	if (ret)
		goto final;

	ret = ntrApplyOptions(&options);

	if (ret)
		goto final;

	if (!boot)
		showDbg("Settings loaded from file \"%s\"", file_name);

final:
	close_sdmc_archive();
	return 0;
}

s32 ntrMenuDeleteOptions(void)
{
	s32 ret = open_sdmc_archive();
	if (ret)
		return ret;

	char file_name[PATH_MAX];
	ret = strnjoin(file_name, PATH_MAX, ntrConfig->ntrFilePath, SETTINGS_FILE_NAME);
	if (ret != 0) {
		showDbg("Settings file name too long: \"%s%s\"", ntrConfig->ntrFilePath, SETTINGS_FILE_NAME);
		goto final;
	}
	FS_Path file_path = fsMakePath(PATH_ASCII, file_name);

	Handle file;
	ret = FSUSER_OpenFile(&file, sdmc_archive, file_path, FS_OPEN_READ, 0);
	if (ret) {
		showDbg("Settings file \"%s\" cannot be opened: %"PRIx32, file_name, ret);
		goto final;
	}
	FSFILE_Close(file);
	file = 0;

	ret = FSUSER_DeleteFile(sdmc_archive, file_path);
	if (ret) {
		showDbg("Failed to delete settings file \"%s\": %"PRIx32, file_name, ret);
		goto final;
	}

	showDbg("Successfully deleted settings file \"%s\"", file_name);

final:
	close_sdmc_archive();
	return 0;
}

s32 ntrMenuRestoreOptions(void)
{
	struct ntr_options_stage options= { 0 };

	s32 ret = ntrApplyOptions(&options);
	if (ret)
		goto final;

	showDbg("Settings restored to default.");

final:
	return 0;
}
