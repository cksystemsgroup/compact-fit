/* 
 * Copyright (c) Hannes Payer hpayer@cosy.sbg.ac.at 
 * cs.uni-salzburg.at/~hpayer
 *
 * University Salzburg, www.uni-salzburg.at
 * Department of Computer Science, cs.uni-salzburg.at
 */

/*  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "cf.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv){
	
	/*init memory - using system call sbrk*/
	void *memory;
	memory = sbrk(10000000);
	cf_init(10000000, memory);
	
	int i;
	void **ptr[1000];
	
	/*allocation*/
	for(i=0; i<1000; i++){
		ptr[i] = cf_malloc(88);
	}
	
	cf_print_pages_status();
	
	/*deallocation*/
	for(i=0; i<1000; i++){
		if(i%2==0){
			cf_free(ptr[i]);
		}
	}
	cf_print_pages_status();

	/*allocation*/
	for(i=0; i<1000; i++){
		if(i%2==0){
			ptr[i] = cf_malloc(88);
		}
	}
	cf_print_pages_status();
	
	/*deallocation*/
	for(i=0; i<1000; i++){
		if(i%2==1){
			cf_free(ptr[i]);
		}
	}
	cf_print_pages_status();

	/*allocation*/
	for(i=0; i<1000; i++){
		if(i%2==1){
			ptr[i] = cf_malloc(88);
		}
	}
	cf_print_pages_status();
	
	/*deallocation*/
	for(i=0; i<1000; i++){
		cf_free(ptr[i]);
	}
	cf_print_pages_status();
	
	printf("- end of program -\n");

	return 0;

}
