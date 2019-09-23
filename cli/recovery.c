/*
 * Microsemi Switchtec(tm) PCIe Management Command Line Interface
 * Copyright (c) 2017, Microsemi Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "commands.h"
#include "argconfig.h"
#include "suffix.h"
#include "progress.h"
#include "gui.h"
#include "common.h"
#include "progress.h"

#include <switchtec/switchtec.h>
#include <switchtec/utils.h>
#include <switchtec/recovery.h>
#include <switchtec/endian.h>

#include <lib/crc32.h>

#include <locale.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

static const struct argconfig_choice recovery_mode_choices[] = {
	{"I2C", SWITCHTEC_BL2_RECOVERY_I2C, "I2C"},
	{"XMODEM", SWITCHTEC_BL2_RECOVERY_XMODEM, "XModem"},
	{"BOTH", SWITCHTEC_BL2_RECOVERY_I2C_AND_XMODEM,
		"Both I2C and XModem"},
	{}
};

static const struct argconfig_choice secure_state_choices[] = {
	{"INITIALIZED_UNSECURED", SWITCHTEC_INITIALIZED_UNSECURED,
	 	"Unsecured"},
	{"INITIALIZED_SECURED", SWITCHTEC_INITIALIZED_SECURED, "Secured"},
	{}
};

static int ping(int argc, char **argv)
{
	time_t t;
	const char *desc = "Ping firmware and get current boot phase";
	int ret;
	unsigned int reply_dw;
	enum switchtec_boot_phase phase_id;

	static struct {
		struct switchtec_dev *dev;
	} cfg = {};

	const struct argconfig_options opts[] = {
		DEVICE_OPTION,
		{NULL}
	};

	argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));

	t = time(NULL);
	ret = switchtec_ping(cfg.dev, t, &reply_dw);
	if (ret != 0) {
		switchtec_perror("recovery ping");
		return ret;
	}

	if(reply_dw != ~t) {
		fprintf(stderr, "Unexpected ping reply from device.\n");
		return -1;
	}

	ret = switchtec_get_boot_phase(cfg.dev, &phase_id);
	if (ret != 0) {
		switchtec_perror("recovery ping");
		return ret;
	}

	printf("Ping reply received, current boot phase is: ");
	switch(phase_id) {
	case SWITCHTEC_BOOT_PHASE_BL1:
		printf("BL1\n");
		break;

	case SWITCHTEC_BOOT_PHASE_BL2:
		printf("BL2\n");
		break;

	case SWITCHTEC_BOOT_PHASE_FW:
		printf("Main firmware\n");
		break;

	default:
		printf("Unknown phase\n");
		return -2;
	}
	return 0;
}

void print_security_config(struct switchtec_security_cfg_stat *state)
{
	int key_idx;
	int i;
	static char *spi_rate_str[] = {
		"100", "67", "50", "40", "33.33", "28.57",
		"25", "22.22", "20", "18.18"
	};

	printf("Basic secure settings %s\n",
		state->basic_setting_valid? "(valid)":"(invalid)");

	printf("\tJTAG/EJTAG debug state: ");
	switch(state->debug_mode) {
	case SWITCHTEC_DEBUG_MODE_ENABLED:
		printf("always enabled\n");
		break;

	case SWITCHTEC_DEBUG_MODE_DISABLED_BUT_ENABLE_ALLOWED:
		printf("disabled by default but can be enabled\n");
		break;

	case SWITCHTEC_DEBUG_MODE_DISABLED:
		printf("always disabled\n");
		break;

	default:
		printf("unsupported state\n");
		break;
	}

	printf("\tSecure state: ");
	switch(state->secure_state) {
	case SWITCHTEC_UNINITIALIZED_UNSECURED:
		printf("uninitialized, unsecured\n");
		break;

	case SWITCHTEC_INITIALIZED_UNSECURED:
		printf("initialized, unsecured\n");
		break;

	case SWITCHTEC_INITIALIZED_SECURED:
		printf("initialized, secured\n");
		break;

	default:
		printf("unsupported state\n");
		break;

	}

	printf("\tAttest mode: ");
	switch(state->attest_mode) {
	case SWITCHTEC_ATTST_MODE_NONE:
		printf("disabled\n");
		break;

	case SWITCHTEC_ATTST_MODE_DICE:
		printf("enabled\n");
		break;

	default:
		printf("unsupported\n");
		break;
	}

	printf("\tJTAG/EJTAG state after reset: \t%d\n",
		state->jtag_lock_after_reset);

	printf("\tJTAG/EJTAG state after BL1: \t%d\n",
		state->jtag_lock_after_bl1);

	printf("\tJTAG/EJTAG unlock in BL1: \t%d\n",
		state->jtag_bl1_unlock_allowed);

	printf("\tJTAG/EJTAG unlock after BL1: \t%d\n",
		state->jtag_post_bl1_unlock_allowed);

	printf("\tSPI clock rate: %s MHz\n",
		spi_rate_str[state->spi_clk_rate-1]);

	printf("\tI2C recovery tmo: %d second(s)\n",
		state->i2c_recovery_tmo);
	printf("\tI2C port: %d\n", state->i2c_port);
	printf("\tI2C address (7-bits): 0x%02x\n", state->i2c_addr);
	printf("\tI2C command map: 0x%x\n", state->i2c_cmd_map);

	printf("Exponent hex data %s: 0x%08x\n",
		state->public_key_exp_valid? "(valid)":"(invalid)",
		state->public_key_exponent);

	printf("KMSK entry number %s: %d\n",
		state->public_key_num_valid? "(valid)":"(invalid)",
		state->public_key_num);

	printf("Current KMSK index %s: %d\n",
		state->public_key_ver_valid? "(valid)":"(invalid)",
		state->public_key_ver);

	printf("KMSK hex data: \n");
	for(key_idx = 0; key_idx < 4; key_idx++) {
		printf("KMSK entry %d:  ", key_idx);
		for(i = 0; i < 64; i++)	{
			printf("%02x", state->public_key[key_idx][i]);
		}
		printf("\n");
	}

	if((state->attest_mode == SWITCHTEC_ATTST_MODE_DICE) &&
       		(state->secure_state == SWITCHTEC_UNINITIALIZED_UNSECURED)) {
		printf("UDS Data:  ");
		for(i = 0; i < 32; i++) {
			printf("%02x", state->uds_key[i]);
		}
		printf("\n");
	}
}

static int info(int argc, char **argv)
{
	const char *desc = "Display security settings";
	int ret;
	enum switchtec_boot_phase phase_id;

	struct switchtec_sn_ver_info sn_info = {};

	static struct {
		struct switchtec_dev *dev;
	} cfg = {};

	const struct argconfig_options opts[] = {
		DEVICE_OPTION,
		{NULL}
	};

	struct switchtec_security_cfg_stat state = {};

	argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));

	ret = switchtec_get_boot_phase(cfg.dev, &phase_id);
	if (ret != 0) {
		switchtec_perror("recovery info");
		return ret;
	}

	printf("Current boot phase: ");
	switch(phase_id) {
	case SWITCHTEC_BOOT_PHASE_BL1:
		printf("BL1\n");
		break;

	case SWITCHTEC_BOOT_PHASE_FW:
		printf("Main Firmware\n");
		break;

	case SWITCHTEC_BOOT_PHASE_BL2:
	default:
		printf("This command is not available in this phase!\n");
		return -2;
	}

	ret = switchtec_sn_ver_get(cfg.dev, &sn_info);
	if (ret != 0) {
		switchtec_perror("recovery info");
		return ret;
	}
	printf("Chip serial: %08x\n", sn_info.chip_serial);
	printf("Key manifest version: %08x\n", sn_info.ver_km);
	printf("BL2 version: %08x\n", sn_info.ver_bl2);
	printf("Main version: %08x\n", sn_info.ver_main);
	printf("Secure unlock version: %08x\n", sn_info.ver_sec_unlock);

	ret = switchtec_security_config_get(cfg.dev, &state);
	if (ret != 0) {
		switchtec_perror("recovery info");
		return ret;
	}

	print_security_config(&state);

	return 0;
}

static int mailbox(int argc, char **argv)
{
	const char *desc = "Retrieve mailbox logs";
	int ret;

	static struct {
		struct switchtec_dev *dev;
		int out_fd;
		const char *out_filename;
	} cfg = {};

	const struct argconfig_options opts[] = {
		DEVICE_OPTION,
		{"filename", .cfg_type=CFG_FD_WR, .value_addr=&cfg.out_fd,
		  .argument_type=required_positional,
		  .force_default="switchtec_mailbox.log",
		  .help="file to log mailbox data"},
		{NULL}
	};

	argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));

	ret = switchtec_mailbox_get(cfg.dev, cfg.out_fd);
	if(ret) {
		switchtec_perror("recovery mailbox");
		close(cfg.out_fd);
		return ret;
	}

	close(cfg.out_fd);
	return 0;
}

void print_image_list(struct switchtec_active_index *idx)
{
	printf("Image\t\tIndex\n");
	printf("key manifest\t%d\n", idx->keyman);
	printf("BL2\t\t%d\n", idx->bl2);
	printf("config\t\t%d\n", idx->config);
	printf("firmware\t%d\n", idx->firmware);
}

static int image_list(int argc, char **argv)
{

	const char *desc = "Display active image list";
	int ret;
	enum switchtec_boot_phase phase_id;

	static struct {
		struct switchtec_dev *dev;
	} cfg = {};

	const struct argconfig_options opts[] = {
		DEVICE_OPTION,
		{NULL}
	};
	struct switchtec_active_index index;

	argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));

	ret = switchtec_get_boot_phase(cfg.dev, &phase_id);
	if (ret != 0) {
		switchtec_perror("image list");
		return ret;
	}
	if(phase_id > SWITCHTEC_BOOT_PHASE_BL1) {
		printf("This command is not available in this phase!\n");
		return -1;
	}

	ret = switchtec_active_image_index_get(cfg.dev, &index);
	if (ret != 0) {
		switchtec_perror("image list");
		return ret;
	}

	print_image_list(&index);

	return 0;
}

static int image_select(int argc, char **argv)
{

	const char *desc = "Select active image index";
	int ret;
	enum switchtec_boot_phase phase_id;

	static struct {
		struct switchtec_dev *dev;
		unsigned char bl2;
		unsigned char firmware;
		unsigned char config;
		unsigned char keyman;
	} cfg = {};

	const struct argconfig_options opts[] = {
		DEVICE_OPTION,
		{"bl2", 'b', "", CFG_BYTE, &cfg.bl2, required_argument,
            		"Active image index for BL2"},
		{"firmware", 'm', "", CFG_BYTE, &cfg.firmware,
			required_argument,
            		"Active image index for firmware"},
		{"config", 'c', "", CFG_BYTE, &cfg.config, required_argument,
            		"Active image index for config"},
		{"keyman", 'k', "", CFG_BYTE, &cfg.keyman, required_argument,
            		"Active image index for key manifest"},
		{NULL}
	};
	struct switchtec_active_index index;

	cfg.bl2 = SWITCHTEC_ACTIVE_INDEX_NOT_SET;
	cfg.firmware = SWITCHTEC_ACTIVE_INDEX_NOT_SET;
	cfg.config = SWITCHTEC_ACTIVE_INDEX_NOT_SET;
	cfg.keyman = SWITCHTEC_ACTIVE_INDEX_NOT_SET;
	argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));

	ret = switchtec_get_boot_phase(cfg.dev, &phase_id);
	if (ret != 0) {
		switchtec_perror("image select");
		return ret;
	}
	if(phase_id > SWITCHTEC_BOOT_PHASE_BL1) {
		printf("This command is not available in this phase!\n");
		return -1;
	}

	if(cfg.bl2 > 1 && cfg.bl2 != SWITCHTEC_ACTIVE_INDEX_NOT_SET) {
		fprintf(stderr, "Active index of BL2 must be within 0-1!\n");
		return -1;
	}
	index.bl2 = cfg.bl2;

	if(cfg.firmware > 1 &&
	   cfg.firmware != SWITCHTEC_ACTIVE_INDEX_NOT_SET) {
		fprintf(stderr,
			"Active index of firmware must be within 0-1!\n");
		return -1;
	}
	index.firmware = cfg.firmware;

	if(cfg.config > 1 && cfg.config != SWITCHTEC_ACTIVE_INDEX_NOT_SET) {
		fprintf(stderr,
			"Active index of config must be within 0-1!\n");
		return -1;
	}
	index.config = cfg.config;

	if(cfg.keyman > 1 && cfg.keyman != SWITCHTEC_ACTIVE_INDEX_NOT_SET) {
		fprintf(stderr,
			"Active index of key manifest must be within 0-1!\n");
		return -1;
	}
	index.keyman = cfg.keyman;

	ret = switchtec_active_image_index_set(cfg.dev, &index);
	if (ret != 0) {
		switchtec_perror("image select");
		return ret;
	}

	return ret;
}

static const char *get_basename(const char *buf)
{
	const char *slash = strrchr(buf, '/');

	if (slash)
		return slash+1;

	return buf;
}

static int check_and_print_fw_image(int img_fd, const char *img_filename)
{
	int ret;
	struct switchtec_fw_image_info info;
	ret = switchtec_fw_file_info(img_fd, &info);

	if (ret != 0) {
        	return -1;
    	}

	printf("File:     %s\n", get_basename(img_filename));
	printf("Type:     %s\n", switchtec_fw_image_type(&info));
	printf("Version:  %s\n", info.version);
	printf("Img Len:  0x%" FMT_SIZE_T_x "\n", info.image_len);
	printf("CRC:      0x%08lx\n", info.image_crc);

	return info.type;
}

static int fw_transfer(int argc, char **argv)
{
	int ret;
	int type;
	enum switchtec_boot_phase phase_id;

	const char *desc = "Transfer a firmware image to device";
	static struct {
		struct switchtec_dev *dev;
		FILE *fimg;
        	const char *img_filename;
		int confirm;
		int dont_execute;
		int force;
		enum switchtec_bl2_recovery_mode bl2_rec_mode;
	} cfg = {};
	const struct argconfig_options opts[] = {
		DEVICE_OPTION,
        	{"img_file", .cfg_type=CFG_FILE_R, .value_addr=&cfg.fimg,
        		.argument_type=required_positional,
        		.help="firmware image file to transfer"},
        	{"yes", 'y', "", CFG_NONE, &cfg.confirm, no_argument,
        		"double confirm before execution"},
        	{"dont-execute", 'E', "", CFG_NONE, &cfg.dont_execute,
			no_argument,
        		"don't execute the new image, use fw-execute to do "
        		"so when it is safe"},
        	{"force", 'f', "", CFG_NONE, &cfg.force, no_argument,
        		"force interrupting an existing fw-update command "
        		"in case firmware is stuck in the busy state"},
		{"bl2_recovery_mode", 'm', "bl2_recovery_mode",
			CFG_CHOICES, &cfg.bl2_rec_mode,
        		required_argument, "BL2 recovery mode",
			.choices=recovery_mode_choices},
        	{NULL}
	};

	argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));

	ret = switchtec_get_boot_phase(cfg.dev, &phase_id);
	if (ret != 0) {
		switchtec_fw_perror("security fw-transfer", ret);
		return ret;
	}
	if(phase_id > SWITCHTEC_BOOT_PHASE_BL1) {
		printf("This command is not available in this phase!\n");
		return -1;
	}

	printf("Writing the following firmware image to %s:\n",
        	switchtec_name(cfg.dev));

	type = check_and_print_fw_image(fileno(cfg.fimg), cfg.img_filename);
	if (type < 0) {
		fprintf(stderr, "%s - Invalid image file format!\n",
        		cfg.img_filename);
		fclose(cfg.fimg);
		return type;
	}

	ret = ask_if_sure(!cfg.confirm);
	if (ret) {
		fclose(cfg.fimg);
		return ret;
	}

	progress_start();
	ret = switchtec_fw_write_file_ex(cfg.dev, MRPC_FW_TX, cfg.fimg, cfg.dont_execute,
                      cfg.force, progress_update);
	fclose(cfg.fimg);

	if (ret) {
		printf("\n");
		switchtec_fw_perror("security fw-transfer", ret);
		return -1;
	}

	progress_finish();
    	printf("\n");

	if(cfg.dont_execute)
		return 0;

	ret = switchtec_fw_exec(cfg.dev, cfg.bl2_rec_mode);
	if (ret) {
		switchtec_fw_perror("security fw-transfer", ret);
		return ret;
	}

	return 0;
}

static int fw_execute(int argc, char **argv)
{
	int ret;
	enum switchtec_boot_phase phase_id;

	const char *desc = "Execute the transferred firmware image";
	static struct {
		struct switchtec_dev *dev;
		int confirm;
		enum switchtec_bl2_recovery_mode bl2_rec_mode;
	} cfg = {};
	const struct argconfig_options opts[] = {
		DEVICE_OPTION,
        	{"yes", 'y', "", CFG_NONE, &cfg.confirm, no_argument,
        		"double confirm before execution"},
		{"bl2_recovery_mode", 'm', "", CFG_CHOICES, &cfg.bl2_rec_mode,
        		required_argument, "BL2 recovery mode",
			.choices = recovery_mode_choices},
        	{NULL}
	};

	argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));

	ret = switchtec_get_boot_phase(cfg.dev, &phase_id);
	if (ret != 0) {
		switchtec_perror("security fw-execute");
		return ret;
	}
	if(phase_id > SWITCHTEC_BOOT_PHASE_BL1) {
		printf("This command is not available in this phase!\n");
		return -1;
	}

	ret = ask_if_sure(!cfg.confirm);
	if (ret) {
		return ret;
	}

	ret = switchtec_fw_exec(cfg.dev, cfg.bl2_rec_mode);
	if (ret) {
		switchtec_fw_perror("security fw-execute", ret);
		return ret;
	}

	return 0;
}

static int load_uds_from_file(FILE *uds_file, unsigned char *uds)
{
	size_t rlen;
	rlen = fread(uds, 1, SWITCHTEC_UDS_LEN, uds_file);

	if(rlen < SWITCHTEC_UDS_LEN)
		return -1;
	return 0;
}

static int security_config_set(int argc, char **argv)
{
	int ret;
	struct switchtec_security_cfg_set settings = {};
	enum switchtec_boot_phase phase_id;

	const char *desc = "Set the device security settings";
	static struct {
		struct switchtec_dev *dev;
		FILE *setting_fimg;
		char *setting_file;
		FILE *uds_fimg;
		char *uds_file;
	} cfg = {};
	const struct argconfig_options opts[] = {
		DEVICE_OPTION,
		{"setting_file", .cfg_type=CFG_FILE_R,
			.value_addr=&cfg.setting_fimg,
        		.argument_type=required_positional,
        		.help="security setting file"},
		{"uds_file", 'u', .cfg_type=CFG_FILE_R,
			.value_addr=&cfg.uds_fimg,
        		.argument_type=required_argument,
        		.help="UDS file"},
        	{NULL}
	};

	argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));

	ret = switchtec_get_boot_phase(cfg.dev, &phase_id);
	if (ret != 0) {
		switchtec_perror("recovery config-set");
		return ret;
	}
	if(phase_id == SWITCHTEC_BOOT_PHASE_BL2 ||
	   phase_id > SWITCHTEC_BOOT_PHASE_FW) {
		printf("This command is not available in this phase!\n");
		return -1;
	}

	ret = switchtec_read_sec_cfg_file(cfg.setting_fimg, &settings);
	fclose(cfg.setting_fimg);
	if(ret) {
		fprintf(stderr, "Invalid secure setting file: %s!\n",
			cfg.setting_file);
		return -1;
	}

	if(settings.attest_mode == SWITCHTEC_ATTST_MODE_DICE &&
	   (settings.uds_valid == 0) && (cfg.uds_file == NULL)) {
		fprintf(stderr,
			"UDS needs to be provided for DICE mode!\n");
		return -1;
	}

	if(cfg.uds_file) {
		ret = load_uds_from_file(cfg.uds_fimg, settings.uds);
		fclose(cfg.uds_fimg);
		if(ret) {
			fprintf(stderr, "Invalid UDS file %s!\n",
				cfg.uds_file);
			return -1;
		}
		if(settings.attest_mode == SWITCHTEC_ATTST_MODE_DICE)
			settings.uds_valid = 1;
	}

	ret = switchtec_security_config_set(cfg.dev, &settings);
	if (ret) {
		switchtec_perror("recovery config-set");
		return ret;
	}

	return 0;
}

int load_sig_from_file(FILE *sig_fimg, unsigned char *sig)
{
	ssize_t rlen;

	rlen = fread(sig, 1, SWITCHTEC_SIG_LEN, sig_fimg);

	if(rlen < SWITCHTEC_SIG_LEN)
		return -1;

	return 0;
}

static int kmsk_add(int argc, char **argv)
{
	int ret;
	unsigned char kmsk[64];
	unsigned char pubk[512];
	unsigned char sig[512];
	unsigned int exponent;
	enum switchtec_boot_phase phase_id;

	const char *desc = "Add a KSMK entry";
	static struct {
		struct switchtec_dev *dev;
		FILE *pubk_fimg;
		char *pubk_file;
		FILE *sig_fimg;
		char *sig_file;
		FILE *kmsk_fimg;
		char *kmsk_file;
	} cfg = {};
	const struct argconfig_options opts[] = {
		DEVICE_OPTION,
		{"pub_key_file", 'p', .cfg_type=CFG_FILE_R,
			.value_addr=&cfg.pubk_fimg,
        		.argument_type=required_argument,
        		.help="public key file"},
		{"signature_file", 's', .cfg_type=CFG_FILE_R,
			.value_addr=&cfg.sig_fimg,
        		.argument_type=required_argument,
        		.help="signature file"},
		{"kmsk_entry_file", 'k', .cfg_type=CFG_FILE_R,
			.value_addr=&cfg.kmsk_fimg,
        		.argument_type=required_argument,
        		.help="KMSK entry file"},
        	{NULL}
	};

	argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));

	ret = switchtec_get_boot_phase(cfg.dev, &phase_id);
	if (ret != 0) {
		switchtec_perror("recovery kmsk-entry-add");
		return ret;
	}
	if(phase_id == SWITCHTEC_BOOT_PHASE_BL2 ||
	   phase_id > SWITCHTEC_BOOT_PHASE_FW) {
		printf("This command is not available in this phase!\n");
		return -1;
	}

	ret = switchtec_read_kmsk_file(cfg.kmsk_fimg, kmsk);
	fclose(cfg.kmsk_fimg);

	if(ret) {
		fprintf(stderr, "Invalid KMSK file %s!\n", cfg.kmsk_file);
		return -1;
	}

	if(cfg.pubk_file) {
		ret = switchtec_read_pubk_file(cfg.pubk_fimg, pubk, &exponent);
		fclose(cfg.pubk_fimg);

		if(ret) {
			fprintf(stderr, "Invalid public key file %s!\n",
				cfg.pubk_file);
			return -1;
		}
	}

	if(cfg.sig_file) {
		ret = load_sig_from_file(cfg.sig_fimg, sig);
		fclose(cfg.sig_fimg);

		if(ret) {
			fprintf(stderr, "Invalid signature file %s!\n",
				cfg.sig_file);
			return -1;
		}
	}

	if(cfg.pubk_file && cfg.sig_file) {
		ret = switchtec_kmsk_set(cfg.dev, pubk, exponent,
				sig, kmsk);

	}
	else {
		ret = switchtec_kmsk_set(cfg.dev, NULL, 0,
				NULL, kmsk);
	}

	if(ret)
		switchtec_perror("recovery kmsk-entry-add");

	return ret;
}

static int secure_state_set(int argc, char **argv)
{
	int ret;
	enum switchtec_boot_phase phase_id;

	const char *desc = "Set device secure state";
	static struct {
		struct switchtec_dev *dev;
		enum switchtec_secure_state state;
	} cfg = {};
	const struct argconfig_options opts[] = {
		DEVICE_OPTION,
		{"state", 't', "state",
			CFG_CHOICES, &cfg.state,
        		required_argument, "secure state",
			.choices=secure_state_choices},
        	{NULL}
	};

	argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));

	ret = switchtec_get_boot_phase(cfg.dev, &phase_id);
	if (ret != 0) {
		switchtec_perror("recovery state-set");
		return ret;
	}
	if(phase_id == SWITCHTEC_BOOT_PHASE_BL2 ||
	   phase_id > SWITCHTEC_BOOT_PHASE_FW) {
		printf("This command is not available in this phase!\n");
		return -1;
	}

	ret = switchtec_secure_state_set(cfg.dev, cfg.state);
	if(ret)
		switchtec_perror("recovery state-set");

	return ret;
}

static int boot_resume(int argc, char **argv)
{
	int ret;
	enum switchtec_boot_phase phase_id;

	const char *desc = "Resume device boot";
	static struct {
		struct switchtec_dev *dev;
	} cfg = {};
	const struct argconfig_options opts[] = {
		DEVICE_OPTION,
        	{NULL}
	};

	argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));

	ret = switchtec_get_boot_phase(cfg.dev, &phase_id);
	if (ret != 0) {
		switchtec_perror("recovery boot-resume");
		return ret;
	}
	if(phase_id > SWITCHTEC_BOOT_PHASE_BL2) {
		printf("This command is not available in this phase!\n");
		return -1;
	}

	ret = switchtec_boot_resume(cfg.dev);
	if(ret)
		switchtec_perror("recovery boot-resume");

	return ret;
}

static int dport_unlock(int argc, char **argv)
{
	int ret;
	unsigned char pubk[SWITCHTEC_PUB_KEY_LEN];
	unsigned char sig[SWITCHTEC_SIG_LEN];
	unsigned int exponent;

	const char *desc = "Unlock debug port";
	static struct {
		struct switchtec_dev *dev;
		FILE *pubkey_fimg;
		char *pubkey_file;
		unsigned int unlock_version;
		unsigned int serial;
		FILE *sig_fimg;
		char *sig_file;
	} cfg = {};
	const struct argconfig_options opts[] = {
		DEVICE_OPTION,
		{"pub_key", 'p', .cfg_type=CFG_FILE_R,
			.value_addr=&cfg.pubkey_fimg,
        		.argument_type=required_argument,
        		.help="public key file"},
		{"serial_number", 'n', .cfg_type=CFG_LONG,
			.value_addr=&cfg.serial,
        		.argument_type=required_argument,
        		.help="device serial number"},
		{"unlock_version", 'v', .cfg_type=CFG_LONG,
			.value_addr=&cfg.unlock_version,
        		.argument_type=required_argument,
        		.help="unlock version"},
		{"signature_file", 's', .cfg_type=CFG_FILE_R,
			.value_addr=&cfg.sig_fimg,
        		.argument_type=required_argument,
        		.help="signature file"},
        	{NULL}
	};

	argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));

	ret = switchtec_read_pubk_file(cfg.pubkey_fimg, pubk, &exponent);
	fclose(cfg.pubkey_fimg);

	if(ret) {
		fprintf(stderr, "Invalid public key file %s!\n",
			cfg.pubkey_file);
		return -1;
	}

	ret = load_sig_from_file(cfg.sig_fimg, sig);
	fclose(cfg.sig_fimg);

	if(ret) {
		fprintf(stderr, "Invalid signature file %s!\n",
			cfg.sig_file);
		return -1;
	}

	ret = switchtec_dport_unlock(cfg.dev, cfg.serial, cfg.unlock_version,
			pubk, exponent, sig);
	if(ret)
		switchtec_perror("recovery dport-unlock");

	return ret;
}

static int dport_lock_update(int argc, char **argv)
{
	int ret;
	unsigned char pubk[SWITCHTEC_PUB_KEY_LEN];
	unsigned char sig[SWITCHTEC_SIG_LEN];
	unsigned int exponent;

	const char *desc = "Unlock debug port";
	static struct {
		struct switchtec_dev *dev;
		FILE *pubkey_fimg;
		char *pubkey_file;
		unsigned int unlock_version;
		unsigned int serial;
		FILE *sig_fimg;
		char *sig_file;
		unsigned int confirm;
	} cfg = {};
	const struct argconfig_options opts[] = {
		DEVICE_OPTION,
		{"pub_key", 'p', .cfg_type=CFG_FILE_R,
			.value_addr=&cfg.pubkey_fimg,
        		.argument_type=required_argument,
        		.help="public key file"},
		{"serial_number", 'n', .cfg_type=CFG_LONG,
			.value_addr=&cfg.serial,
        		.argument_type=required_argument,
        		.help="device serial number"},
		{"new_unlock_version", 'v', .cfg_type=CFG_POSITIVE,
			.value_addr=&cfg.unlock_version,
        		.argument_type=required_argument,
        		.help="unlock version"},
		{"signature_file", 's', .cfg_type=CFG_FILE_R,
			.value_addr=&cfg.sig_fimg,
        		.argument_type=required_argument,
        		.help="signature file"},
		{"yes", 'y', "", CFG_NONE, &cfg.confirm, no_argument,
        		"double confirm before update"},
        	{NULL}
	};

	argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));

	ret = ask_if_sure(!cfg.confirm);
	if (ret) {
		return ret;
	}

	ret = switchtec_read_pubk_file(cfg.pubkey_fimg, pubk, &exponent);
	fclose(cfg.pubkey_fimg);
	if(ret) {
		printf("Invalid public key file %s!\n",
			cfg.pubkey_file);
		return -1;
	}

	ret = load_sig_from_file(cfg.sig_fimg, sig);
	fclose(cfg.sig_fimg);
	if(ret) {
		printf("Invalid signature file %s!\n",
			cfg.sig_file);
		return -1;
	}

	ret = switchtec_secure_unlock_version_update(cfg.dev, cfg.serial,
						     cfg.unlock_version,
						     pubk, exponent, sig);
	if(ret)
		switchtec_perror("dport-lock-update");

	return ret;
}

static const struct cmd commands[] = {
	{"ping", ping, "Ping firmware and get current boot phase"},
	{"info", info, "Display security settings"},
	{"mailbox", mailbox, "Retrieve mailbox logs"},
	{"image_list", image_list, "Display active image list"},
	{"image_select", image_select, "Select active image index"},
	{"fw_transfer", fw_transfer, "Transfer a firmware image to device"},
	{"fw_execute", fw_execute, "Execute the firmware image tranferred"},
	{"config_set", security_config_set,
		"Set the device security settings"},
	{"kmsk_entry_add", kmsk_add, "Add a KMSK entry"},
	{"state_set", secure_state_set, "Set the secure state"},
	{"boot_resume", boot_resume, "Resume device boot"},
	{"dport_unlock", dport_unlock, "Unlock debug port"},
	{"dport_lock_update", dport_lock_update,
		"Update secure unlock version"},
	{}
};

static struct subcommand subcmd = {
	.name = "recovery",
	.cmds = commands,
	.desc = "recovery-related commands",
	.long_desc = "These commands control and manage recovery "
		  "related settings.",
};

REGISTER_SUBCMD(subcmd);
