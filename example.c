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

#include "debug.h"
#include "eeprom-fs/eeprom-fs.h"

int main(void)
{
	// Initialise debug output.
	init_debug_uart1();
	// If you don't want spam, replace 2 with 0.
	set_debug(2);

	// Initialise and format filesystem
	init_eepromfs();

	printf("== Writing 'Hello World!' to file 6...\n");
	file_handle_t fh = open_for_write(6);
	fdata_t contents[] = "Hello World!\n\0";
	write(&fh, contents, sizeof(contents));
	close(&fh);

	printf("\n== Reading file 6...\n");
	fh = open_for_read(6);
	fdata_t stored_contents[fh.filesize];
	read(&fh, stored_contents);
	printf("--> %s", stored_contents);

	printf("\n== Deleting file 6...\n");
	delete(6);

	printf("\n== Reading non-existent file 6...\n");
	fh = open_for_read(6);
	read(&fh, stored_contents);

	printf("\n== Writing 'Lorem ipsum ' to file 7...\n");
	fh = open_for_write(7);
	fdata_t lipsum[] = "Lorem ipsum ";
	write(&fh, lipsum, sizeof(lipsum));
	close(&fh);

	printf("\n== Appending 'dolor sit amet...' to file 7...\n");
	fh = open_for_append(7);
	fdata_t lipsum_more[] =
			"dolor sit amet, consectetur adipisicing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.\n\0";
	write(&fh, lipsum_more, sizeof(lipsum_more));
	close(&fh);

	printf("\n== Reading file 7...\n");
	fh = open_for_read(7);
	fdata_t lipsum_stored[fh.filesize];
	read(&fh, lipsum_stored);
	printf("--> %s", lipsum_stored);

	printf("\n== Appending 'cake! ' to file 1337...\n");
	fh = open_for_append(1337);
	fdata_t cake[] = "cake! ";
	write(&fh, cake, sizeof(cake));
	close(&fh);

	printf("\n== Reading file 1337...\n");
	fh = open_for_read(1337);
	fdata_t cake_stored[fh.filesize];
	read(&fh, cake_stored);
	// Cake isn't null-terminated, so print char by char instead
	printf("--> ");
	for (int i = 0; i < fh.filesize; i++)
	{
		printf("%c", cake_stored[i]);
	}
	printf("\n");

	printf("\n== Dumping EEPROM...\n");
	dump_eeprom();

	for (;;)
		;
}
