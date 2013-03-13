#include <stdio.h>

void aa_test_series(){
	int i;
	for(i=0; i<nr_aas; i++){
		if(i%nr_local_aas){
			assert(aa_space[i] == i-1);
		}
	}
}

void aa_test(int aas, int nr_aa, int nr_ab, int nr_fab){
	int heap = 100000;
	int nr_p = 1;
	int i;

	/*init cf*/	
	cf_init(aas, heap, nr_p, 1, nr_aa, nr_ab, nr_fab, 1, 0, 1, 0);

	/*check globals*/
	assert(nr_local_aas == nr_aa);
	assert(nr_aa_buckets == aas/sizeof(unsigned long)/nr_local_aas);

	/*check initialized buckets*/
	for(i=0; i<nr_aa_buckets; i++){
		assert(aa_buckets[i].local_aas == (i+1)*nr_local_aas-1);
	}

	/*check initialized abstract addresses*/
	aa_test_series();
	
	/*abstract addresses test*/
	struct thread_data *data;
	data = get_thread_data();
	long aa[nr_aas];
	for(i=0; i<nr_aas; i++){
		aa[i] = get_free_aa();
		assert(aa[i] == nr_aas-i-1);
		if(!((i+1)%nr_local_aas)){
			assert(data->aas_count == 0);
			assert(data->abuckets_count == 0);
		}
		else{
			assert(data->aas == nr_aas-i-2);
			assert(data->aas_count > 0);
			assert(data->abuckets_count == 0);
		}
		if(!(i%(nr_local_free_abuckets*nr_local_aas))){
			assert(data->free_abuckets_count == 1);
		}
	}
	//printf("-------%d %d-----------\n", nr_local_free_abuckets, nr_local_abuckets);
	for(i=nr_aas-1; i>=0; i--){
		put_free_aa(aa[i]);
		assert(data->aas == aa[i]);
		if(!(i%nr_local_aas)){
			assert(data->aas_count == nr_local_aas);
		}
		else{
			assert(data->aas == nr_aas-i-1);
			assert(data->aas_count > 0);
		}
		//printf("%d: free abuckets: %d; abuckets count: %d\n", i, data->free_abuckets_count, data->abuckets_count);
	}
	aa_test_series();
	/*abstract addresses higher functions test*/
	void **aa_ptr[nr_aas];
	for(i=0; i<nr_aas; i++){
		aa_ptr[i] = get_abstract_address((void *)(0x60000000+i));
		assert(*aa_ptr[i] == ((void **)(0x60000000+i)));
	}
	assert(data->aas_count == 0);

	for(i=nr_aas-1; i>=0; i--){
		clear_abstract_address(aa_ptr[i]);
	}
	assert(data->aas_count == nr_local_aas);
	aa_test_series();
}

void pages_test_series(){
	struct mem_page *mp;
	struct page *p = pages;
	int i,j;

	for(i=0; i<nr_pages; i++){
		mp = (struct mem_page *)p;
		if(!((i+1)%(nr_local_pages))){
			struct mem_page *tmp = mp;
			for(j=1; j<nr_local_pages; j++){
				assert(tmp->local_pages == (struct mem_page *)(p-j));
				tmp = tmp->local_pages;
			}
			if(i>nr_local_pages){
				assert(mp->next == (struct mem_page *)(p-nr_local_pages));
			}
		}
		p=p+1;
	}

}

void pages_test(unsigned long heap, int nr_p){
	/*init cf*/	
	cf_init(100, heap, nr_p, 1, 2, 1, 1, 1, 0, 1, 0);

	/*check globals*/
	assert(nr_local_pages == nr_p);
	assert(nr_pages <= heap/16384);

	/*check initialized pages*/
	/*p=pages;
	for(i=0; i<nr_pages; i++){
		mp = (struct mem_page *)p;
		printf("%d %p %p %p %p\n", i, p, mp, mp->next, mp->local_pages);
		p+=1;
	}*/
	
	pages_test_series();

	/*pages test*/
	int i;
	struct thread_data *data;
	data = get_thread_data();
	struct page *ps[nr_pages];
	for(i=0; i<nr_pages; i++){
		ps[i] = get_free_page();
		assert(ps[i] == pages+nr_pages-i-1);
		if(!((i+1)%nr_local_pages)){
			assert(data->pages_count == 0);
		}
		else{
			assert(data->pages_count > 0);
		}
	}
	assert(data->pages_count == 0);
	assert(data->pages == NULL);

	for(i=nr_pages-1; i>=0; i--){
		add_free_page(ps[i]);	
	}

	pages_test_series();
}

void pages_multi_buckets_test(unsigned long heap, int nr_p, int buckets){
	cf_init(100, heap, nr_p, buckets, 2, 1, 1, 1, 0, 1, 0);

	int i;
	struct thread_data *data;
	struct page *ps[nr_pages];

	/*init cf*/	
	data = get_thread_data();
	for(i=0; i<nr_pages; i++){
		ps[i] = get_free_page();
	}
	assert(data->pages_count == 0);
	assert(data->pbuckets_count == 0);

	for(i=0; i<nr_pages; i++){
		add_free_page(ps[i]);
		if(buckets == 0){
			assert(data->pbuckets_count == 0);
		}
		else{
			assert(data->pbuckets_count == (i/nr_local_pages)%buckets);
		}
	}
	assert(data->pages_count == nr_local_pages);

	for(i=0; i<nr_pages; i++){
		ps[i] = get_free_page();
	}
		assert(data->pages_count == 0);
	assert(data->pbuckets_count == 0);
	for(i=nr_pages-1; i>=nr_pages; i--){
		add_free_page(ps[i]);
		if(buckets == 0){
			assert(data->pbuckets_count == 0);
		}
		else{
			assert(data->pbuckets_count == (i/nr_local_pages)%buckets);
		}

	}
}


void page_block_test(unsigned long heap, int nr_p){
	struct page *p;
	int nr_pbs = 64;
	int pb_index[nr_pbs];

	/*init cf*/	
	cf_init(100, heap, nr_p, 1, 2, 1, 1, 1, 0, 1, 0);
	

	/*page block test*/
	p = get_free_page();

	int i;
	unsigned int set_bits = 0;
	for(i=0; i<nr_pbs; i++){
		pb_index[i] = get_free_page_block(p);
		assert(p->nr_used_page_blocks == i+1);
		assert(pb_index[i] == i);
		if(i<31){
			assert(p->used_page_block_bitmap[0] == 1);
			assert(p->used_page_block_bitmap[1] == 0);
			set_bits |= (1 << i);
			assert(p->used_page_block_bitmap[2] == set_bits);
		}
		else if(i==31){
			assert(p->used_page_block_bitmap[0] == 1);
			assert(p->used_page_block_bitmap[1] == 1);
			set_bits |= (1 << i);
			assert(p->used_page_block_bitmap[2] == set_bits);
			set_bits = 0;
		}
		else if(i<63){
			assert(p->used_page_block_bitmap[0] == 3);
			assert(p->used_page_block_bitmap[1] == 1);
			set_bits |= (1 << i);
			assert(p->used_page_block_bitmap[3] == set_bits);
		}
		else{
			assert(p->used_page_block_bitmap[0] == 3);
			assert(p->used_page_block_bitmap[1] == 3);
			set_bits |= (1 << i);
			assert(p->used_page_block_bitmap[3] == set_bits);
		}
	}

	int pb;
	for(i=0; i<nr_pbs; i++){
		pb = find_used_page_block(p);
		assert(pb == i);
		free_used_page_block(p, pb);
	}
}

void size_class_test(unsigned long heap, int nr_p, int pcb){
	/*init cf*/	
	cf_init(100, heap, nr_p, 1, 2, 1, 1, pcb, 0, 1, 0);

	int i;
	struct page *p[nr_pages];
	struct thread_data *data;
	struct size_class *sc;
	
	data = get_thread_data();
	sc = &data->size_classes[0];

	/*check initialized size-class mapping*/ 
	assert(sc->partial_compaction_bound == pcb);
	assert(size_class_2_size_map[0] == MINPAGEBLOCKSIZE);
	assert(size_class_2_size_map[SIZECLASSES-1] = PAGEDATASIZE);
	for(i=0; i<nr_pages; i++){
		p[i] = get_free_page();
		add_page_to_full_list(sc, p[i]);
		assert(!list_empty(&sc->head_full_pages));
		assert(sc->nr_pages == i+1);
		assert(sc->max_nr_pages == i+1);
	}

	for(i=nr_pages-1; i>=0; i--){
		assert(!list_empty(&sc->head_full_pages));
		remove_page_from_full_list(sc, p[i]);
		assert(sc->nr_pages == i);
		assert(sc->max_nr_pages == nr_pages);
	}
	assert(list_empty(&sc->head_full_pages));

	for(i=0; i<nr_pages; i++){
		//p[i] = get_free_page();
		add_page_to_nfull_list(sc, p[i]);
		assert(!list_empty(&sc->head_nfull_pages));
		if(i>1){
			assert(valid_nfull_pages_ratio(sc, 0) == 0); 
		}
		assert(sc->nfullentries == i+1);
		assert(sc->max_nfullentries == i+1);
		assert(sc->nr_pages == i+1);
	}

	for(i=nr_pages-1; i>=0; i--){
		assert(!list_empty(&sc->head_nfull_pages));
		remove_page_from_nfull_list(sc, p[i]);
		assert(sc->nr_pages == i);
		assert(sc->max_nr_pages == nr_pages);
		assert(sc->nfullentries == i);
	}
	assert(list_empty(&sc->head_nfull_pages));

	for(i=0; i<nr_pages; i++){
		add_page_to_emptying_list(sc, p[i]);
		assert(!list_empty(&sc->head_emptying_pages));
		assert(sc->sourceentries == i+1);
		assert(sc->max_sourceentries == i+1);
		assert(sc->nr_pages == i+1);		
	}

	for(i=nr_pages-1; i>=0; i--){
		assert(!list_empty(&sc->head_emptying_pages));
		remove_page_from_emptying_list(sc, p[i]);
		assert(sc->nr_pages == i);
		assert(sc->max_nr_pages == nr_pages);
		assert(sc->sourceentries == i);
	}
	assert(list_empty(&sc->head_nfull_pages));
}

void cf_basic_test(unsigned long cas, int nrp, int las, int k){
	struct thread_data *data;
	int nr_10 = PAGEDATASIZE/16+10;
	void **ptr_10[nr_10];
	int nr_14 = 10;
	void **ptr_14[nr_14];
	int nr_16 = 10;
	void **ptr_16[nr_16];
	int i;

	/*init cf*/	
	cf_init((nr_10+nr_14+nr_16)*sizeof(long), cas, nrp, 1, las, 1, 1, k, 0, 1, 0);
	data = get_thread_data();

	/*do test*/
	for(i=0; i<nr_10; i++){
		ptr_10[i] = cf_malloc(10);
		//assert(data->abuckets_count == i+1);
		if(i<PAGEDATASIZE/16){
			assert(data->size_classes[0].nr_pages == 1);
		}
		else{
			assert(data->size_classes[0].nr_pages == 2);
		}
	}
	assert(data->size_classes[0].nfullentries == 1);

	for(i=0; i<nr_14; i++){
		ptr_14[i] = cf_malloc(14);
	}
	assert(data->size_classes[1].nr_pages == 1);

	for(i=0; i<nr_16; i++){
		ptr_16[i] = cf_malloc(16);
	}
	assert(data->size_classes[2].nr_pages == 1);

	int buckets = nr_10+nr_14+nr_16;
	for(i=0; i<nr_10; i+=2){
		cf_free(ptr_10[i]);
		assert(data->size_classes[0].nfullentries <= 1);
		//assert(data->abuckets_count == buckets);
		buckets--;
		if(i>=20){
			assert(data->size_classes[0].nr_pages == 1);
		}
	}

	for(i=1; i<nr_10; i+=2){
		cf_free(ptr_10[i]);
		assert(data->size_classes[0].nfullentries <= 1);
		//assert(data->abuckets_count == buckets);
		buckets--;
	}
	assert(data->size_classes[0].nr_pages == 0);	

	for(i=nr_14-1; i>=0; i--){
		cf_free(ptr_14[i]);
		//assert(data->abuckets_count == buckets);
		buckets--;
	}
	assert(data->size_classes[1].nr_pages == 0);

	for(i=0; i<nr_16; i++){
		cf_free(ptr_16[i]);
		//assert(data->abuckets_count == buckets);
		buckets--;
	}
	assert(data->size_classes[2].nr_pages == 0);
}

//TODO: add destructor
int main(){
	printf("Start unit test...\n");

	printf("- abstract address test ");
	aa_test(222, 1, 1, 1);
	aa_test(800, 2, 1, 1);
	aa_test(199, 3, 1, 1);
	aa_test(133, 4, 1, 1);
	aa_test(30, 2, 2, 2);
	aa_test(199, 3, 2, 2);
	aa_test(133, 4, 3, 3);
	printf("completed\n");

	printf("- pages test ");
	pages_test(200000, 1);
	pages_test(400000, 2);
	pages_test(333333, 3);
	pages_test(431234, 4);
	printf("completed\n");

	printf("- pages multi buckets test ");
	pages_multi_buckets_test(200000, 1, 1);
	pages_multi_buckets_test(300000, 1, 2);
	pages_multi_buckets_test(400000, 2, 2);
	pages_multi_buckets_test(400000, 3, 3);
	printf("completed\n");

	printf("- page-block test ");
	page_block_test(200000, 1);
	printf("completed\n");

	printf("- size-class test ");
	size_class_test(200000, 1, 1);
	printf("completed\n");

	printf("- cf basic test ");
	cf_basic_test(200000, 1, 1, 1);
	printf("completed\n");

	printf("done.\n");
	return 0;
}
