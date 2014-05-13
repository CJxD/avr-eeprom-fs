/*
 eeprom-fs: a micro EEPROM filesystem

 Copyright (c) 2014 Chris Watts (cw17g12@ecs.soton.ac.uk)

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 ==========================================================================

 eeprom-fs is a dynamic wear levelling filesystem that stores data in files
 marked by integers.

 The architecture is based off FAT with the file allocation located statically
 towards the start of the EEPROM memory. Because of this, the FAT table can be subject
 to wearing over several tens of thousands of file writes.
 */

#ifndef EEPROM_FS_H_
#define EEPROM_FS_H_

#define EEPROM_FS_START 0x0
#define EEPROM_FS_SIZE 2048
#define EEPROM_FS_BLOCK_SIZE 32
#define EEPROM_FS_MAX_BLOCKS_PER_FILE 8
/* Prime number is recommended, but not mandatory */
#define EEPROM_FS_MAX_FILES 29

#define EEPROM_FS_META_OFFSET 0
#define EEPROM_FS_ALLOC_TABLE_OFFSET sizeof(fs_meta_t)
#define EEPROM_FS_DATA_OFFSET (EEPROM_FS_ALLOC_TABLE_OFFSET + sizeof(alloc_table))
#define EEPROM_FS_NUM_BLOCKS ((EEPROM_FS_SIZE - EEPROM_FS_DATA_OFFSET) / EEPROM_FS_BLOCK_SIZE)
#define EEPROM_FS_BLOCK_DATA_SIZE (EEPROM_FS_BLOCK_SIZE - sizeof(lba_t))

/*
 * Logical block address type
 */
typedef int16_t lba_t;

/*
 * Filename type
 */
typedef uint16_t fname_t;

/*
 * File data type
 */
typedef char fdata_t;

/*
 * Structures for internal types
 */
typedef struct block
{
	lba_t next_block;
	fdata_t data[EEPROM_FS_BLOCK_DATA_SIZE];
} block_t;

typedef struct file_alloc
{
	size_t filesize;
	lba_t data_block;
} file_alloc_t;

typedef struct fs_meta
{
	size_t block_size;
	uintptr_t start_address;
	size_t fs_size;
	uint16_t max_files;
	uint16_t max_blocks_per_file;
} fs_meta_t;

enum handle_type
{
	FH_READ, FH_WRITE, FH_APPEND
};

typedef struct file_handle
{
	fname_t filename;
	size_t filesize;
	enum handle_type type;
	lba_t first_block;
	lba_t last_block;
} file_handle_t;

typedef enum format_type
{
	FORMAT_FULL, FORMAT_QUICK, FORMAT_WIPE
} format_type_t;

/**
 * Set the debug level of the filesystem
 */
void set_debug(uint8_t level);

/**
 * Initialise the EEPROM filesystem
 */
void init_eepromfs();
/**
 * Format the EEPROM for the filesystem - called by #init_eepromfs() if
 * not already formatted.
 */
void format_eepromfs(format_type_t f);

/**
 * Prepare a file for writing.
 * The file handle returned is given to read/write functions.
 */
file_handle_t open_for_write(fname_t filename);
/**
 * Prepare a file for reading and writing.
 * The file handle returned is given to read/write functions.
 */
file_handle_t open_for_append(fname_t filename);
/**
 * Prepare a file for read only.
 * The file handle returned is given to read/write functions.
 */
file_handle_t open_for_read(fname_t filename);
/**
 * Close a file handle to commit changes.
 * If the file is not closed, the file will be rolled back to
 * its original state.
 */
void close(file_handle_t* fh);

/**
 * Write data to a file handle
 */
void write(file_handle_t* fh, const fdata_t* data, size_t size);
/**
 * Read data from a file handle
 */
void read(file_handle_t* fh, fdata_t* buf);
/**
 * Delete an entire file
 */
void delete(fname_t filename);

/**
 * Display all bytes stored in the EEPROM in a hex-dump format
 */
void dump_eeprom();
/**
 * Remove all data from the EEPROM and replace it with zeros
 */
void wipe_eeprom();



#endif /* EEPROM_FS_H_ */
