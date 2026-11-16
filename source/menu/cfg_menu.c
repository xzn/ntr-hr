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

static s32 ntrWriteOptionsToFile(Handle file)
{
	struct ntr_options_stage options_stage;

	options_stage.rp.rp = *rpConfig;
	options_stage.plg.plg = ntrConfig->ex.plg;
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

	ret = ntrWriteOptionsToFile(file);
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

s32 ntrMenuLoadOptions(void)
{
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
	return 0;
}
