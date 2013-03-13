BEGIN {
	size = 16;
	max_lock = 0;
	max_free = 0;
	print "size,lock_avg,lock_max,compact,free_avg,free_max";
}
{
	if ($1 != "size") {
	    if (size != $1) {
		print size "," sum_lock/i "," max_lock "," sum_compact/i "," sum_free/i "," max_free;
		sum_lock = sum_compact = sum_free = 0;
		size = $1;
		i = 0;
		max_lock = 0;
		max_free = 0;
	    }

	    i = i + 1;
	    sum_lock = sum_lock + $2;
	    if (max_lock < $3) max_lock = $3;
	    sum_compact = sum_compact + $4;
	    sum_free = sum_free + $5;
	    if (max_free < $6) max_free = $6;

	}
}
END {
    print size "," sum_lock/i "," max_lock "," sum_compact/i "," sum_free/i "," max_free;
}
