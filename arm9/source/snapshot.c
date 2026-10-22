/***********************************************************/
/* This source is part of SNEmulDS                         */
/* ------------------------------------------------------- */
/* (c) 1997-1999, 2006-2007 archeide, All rights reserved. */
/***********************************************************/
/*
This program is free software; you can redistribute it and/or 
modify it under the terms of the GNU General Public License as 
published by the Free Software Foundation; either version 2 of 
the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, 
but WITHOUT ANY WARRANTY; without even the implied warranty of 
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
GNU General Public License for more details.
*/

#include <malloc.h>
#include <string.h>
#include "ipcfifoTGDSUser.h"
#include "posixHandleTGDS.h"

#include "common.h"
#include "core.h"
#include "snes.h"
#include "apu.h"
#include "gfx.h"
#include "cfg.h"
#include "snapshot.h"
#include "crc32.h"

int		get_snapshot_name(char *file, uchar nb, char *name){
	FILE *f = NULL;
	TSnapShot_Header *header = NULL;
	if (nb > 8){
		return 0;
	}
	if (strlen(file) < 1){
		return 0;
	}
	file[strlen(file)-1] = '0'+nb; // XXX.SM1 XXXX.SM2 ....
	FS_lock();
	if ((f = fopen(file, "r")) == NULL)
	{
		FS_unlock();
		return 0;
	}
	header = (TSnapShot_Header *)TGDSARM9Malloc(sizeof(TSnapShot_Header));
	if (header == NULL)
	{
		fclose(f);
		FS_unlock();
		return 0;
	}
	if (fread(header, sizeof(TSnapShot_Header), 1, f) != 1)
	{
		TGDSARM9Free(header);
		fclose(f);
		FS_unlock();
		return 0;
	}

	char header_name[17];
	memcpy(header_name, header->name, 16);
	header_name[16] = 0;
	sprintf(name, "Save #%d - %s", nb, header_name);

	TGDSARM9Free(header);
	fclose(f);
	FS_unlock();
	return 1;
}

bool	read_snapshot(char *file, uchar nb){
	FILE *f=NULL;
	int	i;
	bool ok = false;
	TSnapShot_Header *header = NULL;
	TSnapShot *snapshot = NULL;
	u8 *spcFile = NULL;
	u8 *payload = NULL;

	if (nb > 8){
		return false;
	}
	if (strlen(file) < 1){
		return false;
	}
	file[strlen(file)-1] = '0'+nb; // XXX.SM1 XXXX.SM2 ....
	FS_lock();
	f = fopen(file, "r");
	if(f == NULL)
	{
		FS_unlock();
		return false;
	}
	
	header = (TSnapShot_Header *)TGDSARM9Malloc(sizeof(TSnapShot_Header));
	if (header == NULL){
		goto cleanup;
	}
	if (fread(header, 1, sizeof(TSnapShot_Header), f) != sizeof(TSnapShot_Header)){
		goto cleanup;
	}

	/* Probe a v3 envelope. For a legacy v1/v2 file these 16 bytes are the
	 * first 16 bytes of WRAM, so on mismatch we just prepend them and fall
	 * through to the legacy layout (no seek required). */
	{
		TSnapShot_V3Header v3;
		if (fread(&v3, 1, sizeof(v3), f) != sizeof(v3)){
			goto cleanup;
		}
		if (v3.magic[0] != 'S' || v3.magic[1] != 'N' || v3.magic[2] != 'E' ||
		    v3.magic[3] != '3' || v3.version != SNAPSHOT_V3_VERSION){
			/* ---- legacy v1/v2 ---- */
			memcpy(SNESC.RAM, &v3, 16);
			if (fread(SNESC.RAM+16, 1, 0x20000-16, f) != 0x20000-16){
				goto cleanup;
			}
			coherent_user_range_by_size((uint32)SNESC.RAM, (int)0x20000);

			fread(SNESC.VRAM, 1, 0x10000, f);
			coherent_user_range_by_size((uint32)SNESC.VRAM, (int)0x10000);

			fread(SNESC.SRAM, 1, 0x8000, f);
			coherent_user_range_by_size((uint32)SNESC.SRAM, (int)0x8000);

			for (i = 0; i < 256; i++)
			{
				uint8	pal[4];
				fread(pal, 1, 3, f);
				coherent_user_range_by_size((uint32)&pal, 4);
				GFX.SNESPal[i] = (pal[2]>>1)|((pal[1]>>1)<<5)|((pal[0]>>1)<<10);
			}

			fread(PPU_PORT, 1, 2*0x90, f);
			coherent_user_range_by_size((uint32)&PPU_PORT[0], 2*0x90);

			fread(EMPTYMEM, 1, 2*0x70, f); // unused
			coherent_user_range_by_size((uint32)EMPTYMEM, 2*0x70);

			fread(DMA_PORT, 1, 2*0x180, f);
			coherent_user_range_by_size((uint32)&DMA_PORT[0], 2*0x180);

			fread(EMPTYMEM, 1, 2*0x80, f); // unused
			coherent_user_range_by_size((uint32)EMPTYMEM, 2*0x80);

			snapshot = (TSnapShot *)TGDSARM9Malloc(sizeof(TSnapShot));
			if (snapshot == NULL){
				goto cleanup;
			}
			if (fread(snapshot, 1, sizeof(TSnapShot), f) != sizeof(TSnapShot)){
				goto cleanup;
			}

			CPU.A  = snapshot->A;  CPU.X  = snapshot->X;  CPU.Y  = snapshot->Y;
			CPU.S  = snapshot->S;  CPU.P  = snapshot->P;  CPU.D  = snapshot->D;
			CPU.PB = snapshot->PB; CPU.DB = snapshot->DB; CPU.PC = snapshot->PC;
			GFX.BG_scroll_reg = snapshot->BG_scroll_reg;
			GFX.SC_incr = snapshot->SC_incr; GFX.FS_incr = snapshot->FS_incr;
			GFX.OAM_upper_byte = snapshot->OAM_upper_byte;
			SNES.PPU_NeedMultiply = snapshot->PPU_NeedMultiply;

			// Only unpack layouts this build understands.
			if (snapshot->options[0] >= 1 && snapshot->options[0] <= SNAPSHOT_OPTIONS_VERSION)
			{
				unpackOptions(snapshot->options[0], &snapshot->options[1]);
			}

			if (CFG.Sound_output) {
				spcFile = TGDSARM9Malloc(0x10200);
				if (spcFile == NULL){
					goto cleanup;
				}
				memset(spcFile, 0, 0x10200);
				if (fread(spcFile, 1, 0x10200, f) != 0x10200){
					goto cleanup;
				}
				APU_stop();
				APU_loadSpc(spcFile);
			}
			ok = true;
		}
		else
		{
			/* ---- v3: validate the CRC32 over the whole payload BEFORE
			 * unpacking, so a partial write / corruption is rejected
			 * without touching emulator state. ---- */
			uint32 plen = v3.payload_len;
			u32    flags = v3.flags;
			u8    *p;
			TSnapShot *snap;

			if (plen == 0){
				goto cleanup;
			}
			payload = (u8*)TGDSARM9Malloc(plen);
			if (payload == NULL){
				goto cleanup;
			}
			if (fread(payload, 1, plen, f) != plen){
				goto cleanup;
			}
			if (crc32(0, payload, plen) != v3.checksum){
				goto cleanup;	/* corrupt / partial write: abort before unpack */
			}

			p = payload;
			memcpy(SNESC.RAM, p, 0x20000); p += 0x20000;
			coherent_user_range_by_size((uint32)SNESC.RAM, (int)0x20000);

			memcpy(SNESC.VRAM, p, 0x10000); p += 0x10000;
			coherent_user_range_by_size((uint32)SNESC.VRAM, (int)0x10000);

			memcpy(SNESC.SRAM, p, 0x8000); p += 0x8000;
			coherent_user_range_by_size((uint32)SNESC.SRAM, (int)0x8000);

			for (i = 0; i < 256; i++)
			{
				GFX.SNESPal[i] = (p[2]>>1)|((p[1]>>1)<<5)|((p[0]>>1)<<10);
				p += 3;
			}

			memcpy(PPU_PORT, p, 2*0x90); p += 2*0x90;
			coherent_user_range_by_size((uint32)&PPU_PORT[0], 2*0x90);

			memcpy((void*)EMPTYMEM, p, 2*0x70); p += 2*0x70; // unused

			memcpy(DMA_PORT, p, 2*0x180); p += 2*0x180;
			coherent_user_range_by_size((uint32)&DMA_PORT[0], 2*0x180);

			memcpy((void*)EMPTYMEM, p, 2*0x80); p += 2*0x80; // unused

			snap = (TSnapShot *)p; p += sizeof(TSnapShot);
			CPU.A  = snap->A;  CPU.X  = snap->X;  CPU.Y  = snap->Y;
			CPU.S  = snap->S;  CPU.P  = snap->P;  CPU.D  = snap->D;
			CPU.PB = snap->PB; CPU.DB = snap->DB; CPU.PC = snap->PC;
			GFX.BG_scroll_reg = snap->BG_scroll_reg;
			GFX.SC_incr = snap->SC_incr; GFX.FS_incr = snap->FS_incr;
			GFX.OAM_upper_byte = snap->OAM_upper_byte;
			SNES.PPU_NeedMultiply = snap->PPU_NeedMultiply;
			if (snap->options[0] >= 1 && snap->options[0] <= SNAPSHOT_OPTIONS_VERSION)
			{
				unpackOptions(snap->options[0], &snap->options[1]);
			}

			/* Complete sprite table + per-line register image (the state the
			 * old format dropped, causing FIXME'd sprite/scanline glitches). */
			memcpy(GFX.spr_info, p, 128*sizeof(sprite_Info)); p += 128*sizeof(sprite_Info);
			memcpy(GFX.spr_info_ext, p, 32); p += 32;
			memcpy(GFX.lineInfo, p, 192*sizeof(t_GFX_lineInfo)); p += 192*sizeof(t_GFX_lineInfo);

			if ((flags & 1) && CFG.Sound_output) {
				APU_stop();
				APU_loadSpc(p);	//blocking, wait APU init
			}
			ok = true;
		}
	}

	GFX.tiles_dirty = 1;
	GFX.Sprites_table_dirty = 1;
	GFX.mode7_copied = 0;	/* force a fresh Mode 7 VRAM copy after load */
	GFX.mode7_dirty = 1;
	CPU.unpacked = 0; // Update ASM  

cleanup:
	if (payload) TGDSARM9Free(payload);
	if (spcFile) TGDSARM9Free(spcFile);
	if (snapshot) TGDSARM9Free(snapshot);
	if (header) TGDSARM9Free(header);
	if (f) fclose(f);
	FS_unlock();
	return ok;
}

bool write_snapshot(char *file, unsigned char nb, const char *name){
	FILE *f=NULL;
	int	i;
	bool ok = false;
	TSnapShot_Header *header = NULL;
	TSnapShot *snapshot = NULL;
	u8 *spcFile = NULL;
	u8 palbuf[0x300];
	TSnapShot_V3Header v3;
	uint32 payload_len, crc;
	u32 flags = 0;

	if (nb > 8){ 
		return false;
	}
	if (strlen(file) < 1){ 
		return false;
	}
	file[strlen(file)-1] = '0'+nb; // XXX.SM1 XXXX.SM2 ....
	FS_lock();
	
	f = fopen(file, "w+");
	if(f == NULL)
	{
		FS_unlock();
		return false;
	}

	/* Grab the SPC payload first so it can be folded into the checksum. */
	if (CFG.Sound_output) {
		spcFile = TGDSARM9Malloc(0x10200);
		if (spcFile == NULL){
			goto cleanup;
		}
		memset(spcFile, 0, 0x10200);
		APU_pause();	// Make sure that the APU is *completely* stopped
		APU_saveSpc(spcFile);	//blocking, wait APU init
		flags |= 1;
	}

	/* Pack the palette into a contiguous 3-byte-per-entry buffer (also
	 * checksummed). */
	for (i = 0; i < 256; i++){
		palbuf[i*3+2] = GFX.SNESPal[i]<<1;
		palbuf[i*3+1] = (GFX.SNESPal[i]>>5)<<1;
		palbuf[i*3+0] = (GFX.SNESPal[i]>>10)<<1;
	}

	snapshot = (TSnapShot *)TGDSARM9Malloc(sizeof(TSnapShot));
	if (snapshot == NULL){
		goto cleanup;
	}
	memset(snapshot, 0, sizeof(TSnapShot));
	snapshot->A = CPU.A; snapshot->X = CPU.X; snapshot->Y = CPU.Y;
	snapshot->S = CPU.S; snapshot->P = CPU.P; snapshot->D = CPU.D;
	snapshot->PB = CPU.PB; snapshot->DB = CPU.DB; snapshot->PC = CPU.PC;

	snapshot->BG_scroll_reg = GFX.BG_scroll_reg;
	snapshot->SC_incr = GFX.SC_incr; snapshot->FS_incr = GFX.FS_incr;
	snapshot->OAM_upper_byte = GFX.OAM_upper_byte;
	snapshot->PPU_NeedMultiply = SNES.PPU_NeedMultiply;

	snapshot->options[0] = SNAPSHOT_OPTIONS_VERSION;
	packOptions(&snapshot->options[1]);
	coherent_user_range_by_size((uint32)snapshot, sizeof(TSnapShot));

	coherent_user_range_by_size((uint32)SNESC.RAM, 0x20000);
	coherent_user_range_by_size((uint32)SNESC.VRAM, 0x10000);
	coherent_user_range_by_size((uint32)SNESC.SRAM, 0x8000);
	coherent_user_range_by_size((uint32)&PPU_PORT[0], 2*0x90);
	coherent_user_range_by_size((uint32)&DMA_PORT[0], 2*0x180);

	/* Payload length and CRC32 (over the payload bytes only, in write order). */
	payload_len = 0x20000 + 0x10000 + 0x8000 + 0x300 + (2*0x90) + (2*0x70) +
	              (2*0x180) + (2*0x80) + (uint32)sizeof(TSnapShot) +
	              (128*sizeof(sprite_Info)) + 32 + (192*sizeof(t_GFX_lineInfo));
	if (flags & 1)
		payload_len += 0x10200;

	crc = 0;
	crc = crc32(crc, SNESC.RAM, 0x20000);
	crc = crc32(crc, SNESC.VRAM, 0x10000);
	crc = crc32(crc, SNESC.SRAM, 0x8000);
	crc = crc32(crc, palbuf, 0x300);
	crc = crc32(crc, PPU_PORT, 2*0x90);
	crc = crc32(crc, (void*)EMPTYMEM, 2*0x70);
	crc = crc32(crc, DMA_PORT, 2*0x180);
	crc = crc32(crc, (void*)EMPTYMEM, 2*0x80);
	crc = crc32(crc, snapshot, sizeof(TSnapShot));
	crc = crc32(crc, GFX.spr_info, 128*sizeof(sprite_Info));
	crc = crc32(crc, GFX.spr_info_ext, 32);
	crc = crc32(crc, GFX.lineInfo, 192*sizeof(t_GFX_lineInfo));
	if (flags & 1)
		crc = crc32(crc, spcFile, 0x10200);

	/* Name header. */
	header = (TSnapShot_Header *)TGDSARM9Malloc(sizeof(TSnapShot_Header));
	if (header == NULL){
		goto cleanup;
	}
	memset(header, 0, sizeof(TSnapShot_Header));
	strncpy(header->name, name, sizeof(header->name)-1);
	header->name[sizeof(header->name)-1] = '\0';
	if (fwrite(header, 1, sizeof(TSnapShot_Header), f) != sizeof(TSnapShot_Header)){
		goto cleanup;
	}

	/* v3 envelope. */
	v3.magic[0] = 'S'; v3.magic[1] = 'N'; v3.magic[2] = 'E'; v3.magic[3] = '3';
	v3.version = SNAPSHOT_V3_VERSION;
	v3.flags = (uint16)flags;
	v3.payload_len = payload_len;
	v3.checksum = crc;
	if (fwrite(&v3, 1, sizeof(v3), f) != sizeof(v3)){
		goto cleanup;
	}

	/* Payload. */
	if (fwrite(SNESC.RAM,  1, 0x20000, f) != 0x20000){ goto cleanup; }
	if (fwrite(SNESC.VRAM, 1, 0x10000, f) != 0x10000){ goto cleanup; }
	if (fwrite(SNESC.SRAM, 1, 0x8000, f) != 0x8000){ goto cleanup; }
	if (fwrite(palbuf, 1, 0x300, f) != 0x300){ goto cleanup; }
	if (fwrite(PPU_PORT, 1, 2*0x90, f) != 2*0x90){ goto cleanup; }
	if (fwrite((void*)EMPTYMEM, 1, 2*0x70, f) != 2*0x70){ goto cleanup; } // unused
	if (fwrite(DMA_PORT, 1, 2*0x180, f) != 2*0x180){ goto cleanup; }
	if (fwrite((void*)EMPTYMEM, 1, 2*0x80, f) != 2*0x80){ goto cleanup; } // unused
	if (fwrite(snapshot, 1, sizeof(TSnapShot), f) != sizeof(TSnapShot)){ goto cleanup; }
	if (fwrite(GFX.spr_info, 1, 128*sizeof(sprite_Info), f) != 128*sizeof(sprite_Info)){ goto cleanup; }
	if (fwrite(GFX.spr_info_ext, 1, 32, f) != 32){ goto cleanup; }
	if (fwrite(GFX.lineInfo, 1, 192*sizeof(t_GFX_lineInfo), f) != 192*sizeof(t_GFX_lineInfo)){ goto cleanup; }
	if (flags & 1) {
		if (fwrite(spcFile, 1, 0x10200, f) != 0x10200){
			APU_pause();	//restore apu
			goto cleanup;
		}
		APU_pause();	//restore apu
	}
	ok = true;

cleanup:
	if (spcFile) TGDSARM9Free(spcFile);
	if (snapshot) TGDSARM9Free(snapshot);
	if (header) TGDSARM9Free(header);
	if (f) fclose(f);
	FS_unlock();
	return ok;
}
