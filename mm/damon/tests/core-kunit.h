/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data Access Monitor Unit Tests
 *
 * Copyright 2019 Amazon.com, Inc. or its affiliates.  All rights reserved.
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#ifdef CONFIG_DAMON_KUNIT_TEST

#ifndef _DAMON_CORE_TEST_H
#define _DAMON_CORE_TEST_H

#include <kunit/test.h>

static void damon_test_regions(struct kunit *test)
{
	struct damon_region *r;
	struct damon_target *t;

	r = damon_new_region(1, 2);
	KUNIT_EXPECT_EQ(test, 1ul, r->ar.start);
	KUNIT_EXPECT_EQ(test, 2ul, r->ar.end);
	KUNIT_EXPECT_EQ(test, 0u, r->nr_accesses);

	t = damon_new_target();
	KUNIT_EXPECT_EQ(test, 0u, damon_nr_regions(t));

	damon_add_region(r, t);
	KUNIT_EXPECT_EQ(test, 1u, damon_nr_regions(t));

	damon_destroy_region(r, t);
	KUNIT_EXPECT_EQ(test, 0u, damon_nr_regions(t));

	damon_free_target(t);
}

static unsigned int nr_damon_targets(struct damon_ctx *ctx)
{
	struct damon_target *t;
	unsigned int nr_targets = 0;

	damon_for_each_target(t, ctx)
		nr_targets++;

	return nr_targets;
}

static void damon_test_target(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_target *t;

	t = damon_new_target();
	KUNIT_EXPECT_EQ(test, 0u, nr_damon_targets(c));

	damon_add_target(c, t);
	KUNIT_EXPECT_EQ(test, 1u, nr_damon_targets(c));

	damon_destroy_target(t, c);
	KUNIT_EXPECT_EQ(test, 0u, nr_damon_targets(c));

	damon_destroy_ctx(c);
}

/*
 * Test kdamond_reset_aggregated()
 *
 * DAMON checks access to each region and aggregates this information as the
 * access frequency of each region.  In detail, it increases '->nr_accesses' of
 * regions that an access has confirmed.  'kdamond_reset_aggregated()' flushes
 * the aggregated information ('->nr_accesses' of each regions) to the result
 * buffer.  As a result of the flushing, the '->nr_accesses' of regions are
 * initialized to zero.
 */
static void damon_test_aggregate(struct kunit *test)
{
	struct damon_ctx *ctx = damon_new_ctx();
	unsigned long saddr[][3] = {{10, 20, 30}, {5, 42, 49}, {13, 33, 55} };
	unsigned long eaddr[][3] = {{15, 27, 40}, {31, 45, 55}, {23, 44, 66} };
	unsigned long accesses[][3] = {{42, 95, 84}, {10, 20, 30}, {0, 1, 2} };
	struct damon_target *t;
	struct damon_region *r;
	int it, ir;

	for (it = 0; it < 3; it++) {
		t = damon_new_target();
		damon_add_target(ctx, t);
	}

	it = 0;
	damon_for_each_target(t, ctx) {
		for (ir = 0; ir < 3; ir++) {
			r = damon_new_region(saddr[it][ir], eaddr[it][ir]);
			r->nr_accesses = accesses[it][ir];
			r->nr_accesses_bp = accesses[it][ir] * 10000;
			damon_add_region(r, t);
		}
		it++;
	}
	kdamond_reset_aggregated(ctx);
	it = 0;
	damon_for_each_target(t, ctx) {
		ir = 0;
		/* '->nr_accesses' should be zeroed */
		damon_for_each_region(r, t) {
			KUNIT_EXPECT_EQ(test, 0u, r->nr_accesses);
			ir++;
		}
		/* regions should be preserved */
		KUNIT_EXPECT_EQ(test, 3, ir);
		it++;
	}
	/* targets also should be preserved */
	KUNIT_EXPECT_EQ(test, 3, it);

	damon_destroy_ctx(ctx);
}

static void damon_test_split_at(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_target *t;
	struct damon_region *r, *r_new;

	t = damon_new_target();
	r = damon_new_region(0, 100);
	r->nr_accesses_bp = 420000;
	r->nr_accesses = 42;
	r->nr_samples = 84;
	r->last_nr_accesses = 15;
	damon_add_region(r, t);
	damon_split_region_at(t, r, 25);
	KUNIT_EXPECT_EQ(test, r->ar.start, 0ul);
	KUNIT_EXPECT_EQ(test, r->ar.end, 25ul);

	r_new = damon_next_region(r);
	KUNIT_EXPECT_EQ(test, r_new->ar.start, 25ul);
	KUNIT_EXPECT_EQ(test, r_new->ar.end, 100ul);

	KUNIT_EXPECT_EQ(test, r->nr_accesses_bp, r_new->nr_accesses_bp);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, r_new->nr_accesses);
	KUNIT_EXPECT_EQ(test, r->nr_samples, r_new->nr_samples);
	KUNIT_EXPECT_EQ(test, r->last_nr_accesses, r_new->last_nr_accesses);

	damon_free_target(t);
	damon_destroy_ctx(c);
}

static void damon_test_merge_two(struct kunit *test)
{
	struct damon_attrs attrs = {
		.sample_interval = 1000,
		.aggr_interval = 100000,
	};
	struct damon_target *t;
	struct damon_region *r, *r2, *r3;
	int i;

	t = damon_new_target();
	r = damon_new_region(0, 100);
	r->nr_accesses = 10;
	r->nr_accesses_bp = 100000;
	damon_add_region(r, t);
	r2 = damon_new_region(100, 300);
	r2->nr_accesses = 20;
	r2->nr_accesses_bp = 200000;
	damon_add_region(r2, t);

	damon_merge_two_regions(t, &attrs, r, r2);
	KUNIT_EXPECT_EQ(test, r->ar.start, 0ul);
	KUNIT_EXPECT_EQ(test, r->ar.end, 300ul);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, 16u);

	i = 0;
	damon_for_each_region(r3, t) {
		KUNIT_EXPECT_PTR_EQ(test, r, r3);
		i++;
	}
	KUNIT_EXPECT_EQ(test, i, 1);

	damon_free_target(t);
}

static void damon_test_merge_two_samples(struct kunit *test)
{
	struct damon_attrs attrs = {
		.sample_interval = 1000,
		.aggr_interval = 100000,
	};
	struct damon_target *t;
	struct damon_region *r, *r2;

	t = damon_new_target();
	r = damon_new_region(0, 100);
	r->nr_accesses = 50;
	r->nr_samples = 200;
	r->samplings_per_aggr = 16;
	damon_add_region(r, t);
	r2 = damon_new_region(100, 300);
	r2->nr_accesses = 400;
	r2->nr_samples = 800;
	r2->samplings_per_aggr = 32;
	damon_add_region(r2, t);

	/* 25% and 50%, in nr_accesses units of the max (100) */
	KUNIT_EXPECT_EQ(test, damon_region_nr_accesses(r, &attrs), 25u);
	KUNIT_EXPECT_EQ(test, damon_region_nr_accesses(r2, &attrs), 50u);

	damon_merge_two_regions(t, &attrs, r, r2);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, 283u);
	KUNIT_EXPECT_EQ(test, r->nr_samples, 600u);
	KUNIT_EXPECT_EQ(test, r->nr_accesses_bp, 47u * 10000);
	/* size weighted: (16 * 100 + 32 * 200) / 300 */
	KUNIT_EXPECT_EQ(test, r->samplings_per_aggr, 26u);

	damon_free_target(t);
}

static void damon_test_adapt_samplings(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_region *r = damon_new_region(0, 4096 * 10);

	/* defaults: lo 25%, hi 75% */
	r->nr_samples = 100;

	r->samplings_per_aggr = 16;
	r->nr_accesses = 90;
	damon_adapt_samplings(c, r);
	KUNIT_EXPECT_EQ(test, r->samplings_per_aggr, 32u);

	r->nr_accesses = 10;
	damon_adapt_samplings(c, r);
	KUNIT_EXPECT_EQ(test, r->samplings_per_aggr, 16u);

	/* 25% and 75% are in the band */
	r->nr_accesses = 25;
	damon_adapt_samplings(c, r);
	KUNIT_EXPECT_EQ(test, r->samplings_per_aggr, 16u);
	r->nr_accesses = 75;
	damon_adapt_samplings(c, r);
	KUNIT_EXPECT_EQ(test, r->samplings_per_aggr, 16u);

	/* the bounds */
	r->samplings_per_aggr = 1;
	r->nr_accesses = 0;
	damon_adapt_samplings(c, r);
	KUNIT_EXPECT_EQ(test, r->samplings_per_aggr, 1u);
	r->samplings_per_aggr = DAMON_MAX_SAMPLINGS_PER_AGGR;
	r->nr_accesses = 100;
	damon_adapt_samplings(c, r);
	KUNIT_EXPECT_EQ(test, r->samplings_per_aggr,
			DAMON_MAX_SAMPLINGS_PER_AGGR);

	/* not sampled: unchanged; unset: the context's default */
	r->samplings_per_aggr = 0;
	r->nr_samples = 0;
	r->nr_accesses = 0;
	damon_adapt_samplings(c, r);
	KUNIT_EXPECT_EQ(test, r->samplings_per_aggr,
			(unsigned int)DAMON_DEFAULT_SAMPLINGS_PER_AGGR);

	damon_free_region(r);
	damon_destroy_ctx(c);
}

static void damon_test_regions_similar(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_region *l = damon_new_region(0, 4096);
	struct damon_region *r = damon_new_region(4096, 8192);

	l->samplings_per_aggr = 16;
	l->nr_samples = 16;
	l->nr_accesses = 8;
	r->samplings_per_aggr = 16;
	r->nr_samples = 16;
	r->nr_accesses = 9;

	/* 8 and 9 accesses differ by 1, 10% of 16 is 1.6 */
	KUNIT_EXPECT_TRUE(test, damon_regions_similar(c, l, r, 10));
	r->nr_accesses = 10;
	KUNIT_EXPECT_FALSE(test, damon_regions_similar(c, l, r, 10));
	KUNIT_EXPECT_TRUE(test, damon_regions_similar(c, l, r, 20));

	/*
	 * 24 is less than 2x of 16: 8 of 16 and 10 of 24 estimate 8 and 10
	 * accesses per aggregation interval, 10% of 24 is 2.4.  The same ratio
	 * on different sampling frequencies is not the same access frequency.
	 */
	r->samplings_per_aggr = 24;
	r->nr_samples = 24;
	r->nr_accesses = 10;
	KUNIT_EXPECT_TRUE(test, damon_regions_similar(c, l, r, 10));
	r->nr_accesses = 11;
	KUNIT_EXPECT_FALSE(test, damon_regions_similar(c, l, r, 10));
	r->nr_accesses = 12;
	KUNIT_EXPECT_FALSE(test, damon_regions_similar(c, l, r, 10));

	/* 2x apart: never similar, even with equal estimated accesses */
	r->samplings_per_aggr = 32;
	r->nr_samples = 32;
	r->nr_accesses = 16;
	KUNIT_EXPECT_FALSE(test, damon_regions_similar(c, l, r, 100));

	/* the estimate uses the samplings per aggregation, not nr_samples */
	r->samplings_per_aggr = 16;
	r->nr_samples = 8;	/* max_aggr_samplings reached early */
	r->nr_accesses = 4;
	KUNIT_EXPECT_TRUE(test, damon_regions_similar(c, l, r, 10));

	damon_free_region(l);
	damon_free_region(r);
	damon_destroy_ctx(c);
}

static void damon_test_region_hotness(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_region *r = damon_new_region(0, 10 * c->min_sz_region);

	/* telescope is off: ten entries of the base page size */
	r->nr_accesses = 50;
	r->nr_samples = 100;
	r->samplings_per_aggr = 64;
	KUNIT_EXPECT_EQ(test, damon_region_hotness(c, r), 10ul * 50 * 64 / 100);

	r->nr_samples = 0;
	KUNIT_EXPECT_EQ(test, damon_region_hotness(c, r), 0ul);

	damon_free_region(r);
	damon_destroy_ctx(c);
}

static void damon_test_sampling_schedule(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_target *t = damon_new_target();
	struct damon_region *r1 = damon_new_region(0, 4096);
	struct damon_region *r2 = damon_new_region(4096, 8192);
	u64 aggr_ns = 100 * NSEC_PER_MSEC, start = 1000, end = start + aggr_ns;
	unsigned int i;

	c->max_aggr_samplings = 3;
	r1->samplings_per_aggr = 2;
	r2->samplings_per_aggr = 8;
	damon_add_region(r1, t);
	damon_add_region(r2, t);
	damon_add_target(c, t);

	kdamond_arm_regions(c, start, aggr_ns);
	KUNIT_EXPECT_TRUE(test, r1->sample_due && r2->sample_due);
	KUNIT_EXPECT_EQ(test, r1->next_sample_ns, start + 50 * NSEC_PER_MSEC);
	KUNIT_EXPECT_EQ(test, r2->next_sample_ns,
			start + 100 * NSEC_PER_MSEC / 8);
	KUNIT_EXPECT_EQ(test, kdamond_next_sample_ns(c, end),
			r2->next_sample_ns);

	kdamond_clear_due(c);
	KUNIT_EXPECT_FALSE(test, r1->sample_due || r2->sample_due);
	KUNIT_EXPECT_FALSE(test, kdamond_mark_due(c, start));

	/* r2 first: due at 12.5 ms, and again at 25 ms */
	KUNIT_EXPECT_TRUE(test, kdamond_mark_due(c, r2->next_sample_ns));
	KUNIT_EXPECT_FALSE(test, r1->sample_due);
	KUNIT_EXPECT_TRUE(test, r2->sample_due);
	kdamond_reschedule_due(c, aggr_ns, end);
	KUNIT_EXPECT_EQ(test, r2->samplings_done, 1u);
	KUNIT_EXPECT_EQ(test, r2->next_sample_ns,
			start + 2 * 100 * NSEC_PER_MSEC / 8);
	KUNIT_EXPECT_TRUE(test, r2->sample_due);

	/* r2 is limited to max_aggr_samplings, r1 to its two samplings */
	for (i = 1; i < 3; i++) {
		kdamond_mark_due(c, r2->next_sample_ns);
		kdamond_reschedule_due(c, aggr_ns, end);
	}
	KUNIT_EXPECT_EQ(test, r2->samplings_done, 3u);
	KUNIT_EXPECT_EQ(test, r2->next_sample_ns, U64_MAX);
	KUNIT_EXPECT_EQ(test, kdamond_next_sample_ns(c, end), r1->next_sample_ns);

	kdamond_mark_due(c, end);
	KUNIT_EXPECT_TRUE(test, r1->sample_due);
	KUNIT_EXPECT_FALSE(test, r2->sample_due);
	kdamond_reschedule_due(c, aggr_ns, end);
	KUNIT_EXPECT_EQ(test, r1->samplings_done, 1u);
	KUNIT_EXPECT_EQ(test, r1->next_sample_ns, end);
	kdamond_mark_due(c, end);
	kdamond_reschedule_due(c, aggr_ns, end);
	KUNIT_EXPECT_EQ(test, r1->next_sample_ns, U64_MAX);
	KUNIT_EXPECT_EQ(test, kdamond_next_sample_ns(c, end), end);

	damon_destroy_ctx(c);
}

static void damon_test_time_stat(struct kunit *test)
{
	struct damon_time_stat stat = {};

	damon_time_stat_add(&stat, 30);
	damon_time_stat_add(&stat, 10);
	damon_time_stat_add(&stat, 20);
	KUNIT_EXPECT_EQ(test, stat.nr, 3u);
	KUNIT_EXPECT_EQ(test, stat.sum, 60ull);
	KUNIT_EXPECT_EQ(test, stat.min, 10ull);
	KUNIT_EXPECT_EQ(test, stat.max, 30ull);
}

static struct damon_region *__nth_region_of(struct damon_target *t, int idx)
{
	struct damon_region *r;
	unsigned int i = 0;

	damon_for_each_region(r, t) {
		if (i++ == idx)
			return r;
	}

	return NULL;
}

static void damon_test_merge_regions_of(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_target *t;
	struct damon_region *r;
	unsigned long sa[] = {0, 100, 114, 122, 130, 156, 170, 184};
	unsigned long ea[] = {100, 112, 122, 130, 156, 170, 184, 230};
	unsigned int nrs[] = {0, 0, 10, 10, 20, 30, 1, 2};

	unsigned long saddrs[] = {0, 114, 130, 156, 170};
	unsigned long eaddrs[] = {112, 130, 156, 170, 230};
	int i;

	t = damon_new_target();
	for (i = 0; i < ARRAY_SIZE(sa); i++) {
		r = damon_new_region(sa[i], ea[i]);
		r->nr_accesses = nrs[i];
		r->nr_accesses_bp = nrs[i] * 10000;
		damon_add_region(r, t);
	}

	damon_merge_regions_of(c, t, DAMON_MERGE_THRES_PCT, 9999);
	/* 0-112, 114-130, 130-156, 156-170 */
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 5u);
	for (i = 0; i < 5; i++) {
		r = __nth_region_of(t, i);
		KUNIT_EXPECT_EQ(test, r->ar.start, saddrs[i]);
		KUNIT_EXPECT_EQ(test, r->ar.end, eaddrs[i]);
	}
	damon_free_target(t);
	damon_destroy_ctx(c);
}

static void __test_telescope_pieces(struct kunit *test, unsigned long start,
		unsigned long end, unsigned int nr, unsigned long *bounds)
{
	struct damon_addr_range pieces[5];
	unsigned int i, n = damon_telescope_pieces(start, end, pieces);

	KUNIT_ASSERT_EQ(test, n, nr);
	for (i = 0; i < n; i++) {
		KUNIT_EXPECT_EQ(test, pieces[i].start, bounds[i]);
		KUNIT_EXPECT_EQ(test, pieces[i].end, bounds[i + 1]);
	}
}

static void damon_test_telescope_pieces(struct kunit *test)
{
	unsigned long pg = PAGE_SIZE;
	unsigned long pmd = PMD_SIZE, pud = PUD_SIZE;
	unsigned long b1[] = {pmd / 2, pmd, 2 * pmd, 5 * pmd / 2};
	unsigned long b2[] = {0, 3 * pud};
	unsigned long b3[] = {pud / 2, pud, 2 * pud, 5 * pud / 2};
	unsigned long b4[] = {pmd / 2 + pg, pmd, pud, 2 * pud,
		5 * pud / 2, 5 * pud / 2 + 2 * pg};
	unsigned long b5[] = {pg, pmd + pg};

	/* A 2 MiB entry inside the region is cut out */
	__test_telescope_pieces(test, pmd / 2, 5 * pmd / 2, 3, b1);
	/* Whole PUDs are one region */
	__test_telescope_pieces(test, 0, 3 * pud, 1, b2);
	/* PUD in the middle, PMD level regions on both sides */
	__test_telescope_pieces(test, pud / 2, 5 * pud / 2, 3, b3);
	/* Five regions, with leaf level regions on both ends */
	__test_telescope_pieces(test, pmd / 2 + pg, 5 * pud / 2 + 2 * pg, 5,
			b4);
	/* Less than a PMD aligned entry stays one leaf level region */
	__test_telescope_pieces(test, pg, pmd + pg, 1, b5);
}

static void damon_test_range_level(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, damon_range_level(0, PUD_SIZE), 2);
	KUNIT_EXPECT_EQ(test, damon_range_level(PUD_SIZE, 3 * PUD_SIZE), 2);
	KUNIT_EXPECT_EQ(test, damon_range_level(PUD_SIZE - PMD_SIZE,
				2 * PUD_SIZE), 1);
	KUNIT_EXPECT_EQ(test, damon_range_level(PMD_SIZE, 3 * PMD_SIZE), 1);
	KUNIT_EXPECT_EQ(test, damon_range_level(PAGE_SIZE, PMD_SIZE), 0);
	KUNIT_EXPECT_EQ(test, damon_range_level(0, PAGE_SIZE), 0);
}

/* With a 2MB minimum region size, regions stay aligned to and sized by 2MB */
static void damon_test_min_sz_region_2mb(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_target *t = damon_new_target();
	struct damon_addr_range range = {
		.start = SZ_1M, .end = 100 * SZ_2M + SZ_1M};
	struct damon_region *r;
	int i;

	c->min_sz_region = SZ_2M;
	damon_add_target(c, t);

	damon_set_regions(t, &range, 1, c->min_sz_region);
	KUNIT_ASSERT_EQ(test, damon_nr_regions(t), 1u);
	r = damon_first_region(t);
	KUNIT_EXPECT_EQ(test, r->ar.start, 0ul);
	KUNIT_EXPECT_EQ(test, r->ar.end, 101 * SZ_2M);

	for (i = 0; i < 10; i++)
		damon_split_regions_of(c, t, 4);
	KUNIT_EXPECT_GT(test, damon_nr_regions(t), 1u);
	damon_for_each_region(r, t) {
		KUNIT_EXPECT_EQ(test, r->ar.start % SZ_2M, 0ul);
		KUNIT_EXPECT_EQ(test, r->ar.end % SZ_2M, 0ul);
		KUNIT_EXPECT_GE(test, damon_sz_region(r), (unsigned long)SZ_2M);
	}

	/* the hotness unit is the leaf entry, not the minimum region size */
	r = damon_first_region(t);
	r->nr_samples = 1;
	r->nr_accesses = 1;
	KUNIT_EXPECT_EQ(test, damon_region_hotness(c, r),
			damon_sz_region(r) / DAMON_MIN_REGION *
			damon_region_samplings(c, r));
	damon_destroy_ctx(c);
}

static void damon_test_split_regions_of(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_target *t;
	struct damon_region *r;

	t = damon_new_target();
	r = damon_new_region(0, 22);
	damon_add_region(r, t);
	damon_split_regions_of(c, t, 2);
	KUNIT_EXPECT_LE(test, damon_nr_regions(t), 2u);
	damon_free_target(t);

	t = damon_new_target();
	r = damon_new_region(0, 220);
	damon_add_region(r, t);
	damon_split_regions_of(c, t, 4);
	KUNIT_EXPECT_LE(test, damon_nr_regions(t), 4u);
	damon_free_target(t);
	damon_destroy_ctx(c);
}

static void damon_test_ops_registration(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_operations ops = {.id = DAMON_OPS_VADDR}, bak;
	bool need_cleanup = false;

	/* DAMON_OPS_VADDR is registered only if CONFIG_DAMON_VADDR is set */
	if (!damon_is_registered_ops(DAMON_OPS_VADDR)) {
		bak.id = DAMON_OPS_VADDR;
		KUNIT_EXPECT_EQ(test, damon_register_ops(&bak), 0);
		need_cleanup = true;
	}

	/* DAMON_OPS_VADDR is ensured to be registered */
	KUNIT_EXPECT_EQ(test, damon_select_ops(c, DAMON_OPS_VADDR), 0);

	/* Double-registration is prohibited */
	KUNIT_EXPECT_EQ(test, damon_register_ops(&ops), -EINVAL);

	/* Unknown ops id cannot be registered */
	KUNIT_EXPECT_EQ(test, damon_select_ops(c, NR_DAMON_OPS), -EINVAL);

	/* Registration should success after unregistration */
	mutex_lock(&damon_ops_lock);
	bak = damon_registered_ops[DAMON_OPS_VADDR];
	damon_registered_ops[DAMON_OPS_VADDR] = (struct damon_operations){};
	mutex_unlock(&damon_ops_lock);

	ops.id = DAMON_OPS_VADDR;
	KUNIT_EXPECT_EQ(test, damon_register_ops(&ops), 0);

	mutex_lock(&damon_ops_lock);
	damon_registered_ops[DAMON_OPS_VADDR] = bak;
	mutex_unlock(&damon_ops_lock);

	/* Check double-registration failure again */
	KUNIT_EXPECT_EQ(test, damon_register_ops(&ops), -EINVAL);

	damon_destroy_ctx(c);

	if (need_cleanup) {
		mutex_lock(&damon_ops_lock);
		damon_registered_ops[DAMON_OPS_VADDR] =
			(struct damon_operations){};
		mutex_unlock(&damon_ops_lock);
	}
}

static void damon_test_set_regions(struct kunit *test)
{
	struct damon_target *t = damon_new_target();
	struct damon_region *r1 = damon_new_region(4, 16);
	struct damon_region *r2 = damon_new_region(24, 32);
	struct damon_addr_range range = {.start = 8, .end = 28};
	unsigned long expects[] = {8, 16, 16, 24, 24, 28};
	int expect_idx = 0;
	struct damon_region *r;

	damon_add_region(r1, t);
	damon_add_region(r2, t);
	damon_set_regions(t, &range, 1, DAMON_MIN_REGION);

	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 3);
	damon_for_each_region(r, t) {
		KUNIT_EXPECT_EQ(test, r->ar.start, expects[expect_idx++]);
		KUNIT_EXPECT_EQ(test, r->ar.end, expects[expect_idx++]);
	}
	damon_destroy_target(t, NULL);
}

static void damon_test_nr_accesses_to_accesses_bp(struct kunit *test)
{
	struct damon_attrs attrs = {
		.sample_interval = 10,
		.aggr_interval = ((unsigned long)UINT_MAX + 1) * 10
	};

	/*
	 * In some cases such as 32bit architectures where UINT_MAX is
	 * ULONG_MAX, attrs.aggr_interval becomes zero.  Calling
	 * damon_nr_accesses_to_accesses_bp() in the case will cause
	 * divide-by-zero.  Such case is prohibited in normal execution since
	 * the caution is documented on the comment for the function, and
	 * damon_update_monitoring_results() does the check.  Skip the test in
	 * the case.
	 */
	if (!attrs.aggr_interval)
		kunit_skip(test, "aggr_interval is zero.");

	KUNIT_EXPECT_EQ(test, damon_nr_accesses_to_accesses_bp(123, &attrs), 0);
}

static void damon_test_update_monitoring_result(struct kunit *test)
{
	struct damon_attrs old_attrs = {
		.sample_interval = 10, .aggr_interval = 1000,};
	struct damon_attrs new_attrs;
	struct damon_region *r = damon_new_region(3, 7);

	r->nr_accesses = 15;
	r->nr_accesses_bp = 150000;
	r->age = 20;

	new_attrs = (struct damon_attrs){
		.sample_interval = 100, .aggr_interval = 10000,};
	damon_update_monitoring_result(r, &old_attrs, &new_attrs, false);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, 15);
	KUNIT_EXPECT_EQ(test, r->age, 2);

	new_attrs = (struct damon_attrs){
		.sample_interval = 1, .aggr_interval = 1000};
	damon_update_monitoring_result(r, &old_attrs, &new_attrs, false);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, 150);
	KUNIT_EXPECT_EQ(test, r->age, 2);

	new_attrs = (struct damon_attrs){
		.sample_interval = 1, .aggr_interval = 100};
	damon_update_monitoring_result(r, &old_attrs, &new_attrs, false);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, 150);
	KUNIT_EXPECT_EQ(test, r->age, 20);

	damon_free_region(r);
}

static void damon_test_set_attrs(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_attrs valid_attrs = {
		.min_nr_regions = 10, .max_nr_regions = 1000,
		.sample_interval = 5000, .aggr_interval = 100000,};
	struct damon_attrs invalid_attrs;

	KUNIT_EXPECT_EQ(test, damon_set_attrs(c, &valid_attrs), 0);

	invalid_attrs = valid_attrs;
	invalid_attrs.min_nr_regions = 1;
	KUNIT_EXPECT_EQ(test, damon_set_attrs(c, &invalid_attrs), -EINVAL);

	invalid_attrs = valid_attrs;
	invalid_attrs.max_nr_regions = 9;
	KUNIT_EXPECT_EQ(test, damon_set_attrs(c, &invalid_attrs), -EINVAL);

	invalid_attrs = valid_attrs;
	invalid_attrs.aggr_interval = 4999;
	KUNIT_EXPECT_EQ(test, damon_set_attrs(c, &invalid_attrs), -EINVAL);

	damon_destroy_ctx(c);
}

static void damon_test_moving_sum(struct kunit *test)
{
	unsigned int mvsum = 50000, nomvsum = 50000, len_window = 10;
	unsigned int new_values[] = {10000, 0, 10000, 0, 0, 0, 10000, 0, 0, 0};
	unsigned int expects[] = {55000, 50000, 55000, 50000, 45000, 40000,
		45000, 40000, 35000, 30000};
	int i;

	for (i = 0; i < ARRAY_SIZE(new_values); i++) {
		mvsum = damon_moving_sum(mvsum, nomvsum, len_window,
				new_values[i]);
		KUNIT_EXPECT_EQ(test, mvsum, expects[i]);
	}
}

static void damos_test_new_filter(struct kunit *test)
{
	struct damos_filter *filter;

	filter = damos_new_filter(DAMOS_FILTER_TYPE_ANON, true, false);
	KUNIT_EXPECT_EQ(test, filter->type, DAMOS_FILTER_TYPE_ANON);
	KUNIT_EXPECT_EQ(test, filter->matching, true);
	KUNIT_EXPECT_PTR_EQ(test, filter->list.prev, &filter->list);
	KUNIT_EXPECT_PTR_EQ(test, filter->list.next, &filter->list);
	damos_destroy_filter(filter);
}

static void damos_test_commit_filter(struct kunit *test)
{
	struct damos_filter *src_filter = damos_new_filter(
		DAMOS_FILTER_TYPE_ANON, true, true);
	struct damos_filter *dst_filter = damos_new_filter(
		DAMOS_FILTER_TYPE_ACTIVE, false, false);

	damos_commit_filter(dst_filter, src_filter);
	KUNIT_EXPECT_EQ(test, dst_filter->type, src_filter->type);
	KUNIT_EXPECT_EQ(test, dst_filter->matching, src_filter->matching);
	KUNIT_EXPECT_EQ(test, dst_filter->allow, src_filter->allow);

	damos_destroy_filter(src_filter);
	damos_destroy_filter(dst_filter);
}

static void damos_test_filter_out(struct kunit *test)
{
	struct damon_target *t;
	struct damon_region *r, *r2;
	struct damos_filter *f;

	f = damos_new_filter(DAMOS_FILTER_TYPE_ADDR, true, false);
	f->addr_range = (struct damon_addr_range){
		.start = DAMON_MIN_REGION * 2, .end = DAMON_MIN_REGION * 6};

	t = damon_new_target();
	r = damon_new_region(DAMON_MIN_REGION * 3, DAMON_MIN_REGION * 5);
	damon_add_region(r, t);

	/* region in the range */
	KUNIT_EXPECT_TRUE(test,
			damos_filter_match(NULL, t, r, f, DAMON_MIN_REGION));
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 1);

	/* region before the range */
	r->ar.start = DAMON_MIN_REGION * 1;
	r->ar.end = DAMON_MIN_REGION * 2;
	KUNIT_EXPECT_FALSE(test,
			damos_filter_match(NULL, t, r, f, DAMON_MIN_REGION));
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 1);

	/* region after the range */
	r->ar.start = DAMON_MIN_REGION * 6;
	r->ar.end = DAMON_MIN_REGION * 8;
	KUNIT_EXPECT_FALSE(test,
			damos_filter_match(NULL, t, r, f, DAMON_MIN_REGION));
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 1);

	/* region started before the range */
	r->ar.start = DAMON_MIN_REGION * 1;
	r->ar.end = DAMON_MIN_REGION * 4;
	KUNIT_EXPECT_FALSE(test,
			damos_filter_match(NULL, t, r, f, DAMON_MIN_REGION));
	/* filter should have split the region */
	KUNIT_EXPECT_EQ(test, r->ar.start, DAMON_MIN_REGION * 1);
	KUNIT_EXPECT_EQ(test, r->ar.end, DAMON_MIN_REGION * 2);
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 2);
	r2 = damon_next_region(r);
	KUNIT_EXPECT_EQ(test, r2->ar.start, DAMON_MIN_REGION * 2);
	KUNIT_EXPECT_EQ(test, r2->ar.end, DAMON_MIN_REGION * 4);
	damon_destroy_region(r2, t);

	/* region started in the range */
	r->ar.start = DAMON_MIN_REGION * 2;
	r->ar.end = DAMON_MIN_REGION * 8;
	KUNIT_EXPECT_TRUE(test,
			damos_filter_match(NULL, t, r, f, DAMON_MIN_REGION));
	/* filter should have split the region */
	KUNIT_EXPECT_EQ(test, r->ar.start, DAMON_MIN_REGION * 2);
	KUNIT_EXPECT_EQ(test, r->ar.end, DAMON_MIN_REGION * 6);
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 2);
	r2 = damon_next_region(r);
	KUNIT_EXPECT_EQ(test, r2->ar.start, DAMON_MIN_REGION * 6);
	KUNIT_EXPECT_EQ(test, r2->ar.end, DAMON_MIN_REGION * 8);
	damon_destroy_region(r2, t);

	damon_free_target(t);
	damos_free_filter(f);
}

static void damon_test_feed_loop_next_input(struct kunit *test)
{
	unsigned long last_input = 900000, current_score = 200;

	/*
	 * If current score is lower than the goal, which is always 10,000
	 * (read the comment on damon_feed_loop_next_input()'s comment), next
	 * input should be higher than the last input.
	 */
	KUNIT_EXPECT_GT(test,
			damon_feed_loop_next_input(last_input, current_score),
			last_input);

	/*
	 * If current score is higher than the goal, next input should be lower
	 * than the last input.
	 */
	current_score = 250000000;
	KUNIT_EXPECT_LT(test,
			damon_feed_loop_next_input(last_input, current_score),
			last_input);

	/*
	 * The next input depends on the distance between the current score and
	 * the goal
	 */
	KUNIT_EXPECT_GT(test,
			damon_feed_loop_next_input(last_input, 200),
			damon_feed_loop_next_input(last_input, 2000));
}

static void damon_test_set_filters_default_reject(struct kunit *test)
{
	struct damos scheme;
	struct damos_filter *target_filter, *anon_filter;

	INIT_LIST_HEAD(&scheme.filters);
	INIT_LIST_HEAD(&scheme.ops_filters);

	damos_set_filters_default_reject(&scheme);
	/*
	 * No filter is installed.  Allow by default on both core and ops layer
	 * filtering stages, since there are no filters at all.
	 */
	KUNIT_EXPECT_EQ(test, scheme.core_filters_default_reject, false);
	KUNIT_EXPECT_EQ(test, scheme.ops_filters_default_reject, false);

	target_filter = damos_new_filter(DAMOS_FILTER_TYPE_TARGET, true, true);
	damos_add_filter(&scheme, target_filter);
	damos_set_filters_default_reject(&scheme);
	/*
	 * A core-handled allow-filter is installed.
	 * Rejct by default on core layer filtering stage due to the last
	 * core-layer-filter's behavior.
	 * Allow by default on ops layer filtering stage due to the absence of
	 * ops layer filters.
	 */
	KUNIT_EXPECT_EQ(test, scheme.core_filters_default_reject, true);
	KUNIT_EXPECT_EQ(test, scheme.ops_filters_default_reject, false);

	target_filter->allow = false;
	damos_set_filters_default_reject(&scheme);
	/*
	 * A core-handled reject-filter is installed.
	 * Allow by default on core layer filtering stage due to the last
	 * core-layer-filter's behavior.
	 * Allow by default on ops layer filtering stage due to the absence of
	 * ops layer filters.
	 */
	KUNIT_EXPECT_EQ(test, scheme.core_filters_default_reject, false);
	KUNIT_EXPECT_EQ(test, scheme.ops_filters_default_reject, false);

	anon_filter = damos_new_filter(DAMOS_FILTER_TYPE_ANON, true, true);
	damos_add_filter(&scheme, anon_filter);

	damos_set_filters_default_reject(&scheme);
	/*
	 * A core-handled reject-filter and ops-handled allow-filter are installed.
	 * Allow by default on core layer filtering stage due to the existence
	 * of the ops-handled filter.
	 * Reject by default on ops layer filtering stage due to the last
	 * ops-layer-filter's behavior.
	 */
	KUNIT_EXPECT_EQ(test, scheme.core_filters_default_reject, false);
	KUNIT_EXPECT_EQ(test, scheme.ops_filters_default_reject, true);

	target_filter->allow = true;
	damos_set_filters_default_reject(&scheme);
	/*
	 * A core-handled allow-filter and ops-handled allow-filter are
	 * installed.
	 * Allow by default on core layer filtering stage due to the existence
	 * of the ops-handled filter.
	 * Reject by default on ops layer filtering stage due to the last
	 * ops-layer-filter's behavior.
	 */
	KUNIT_EXPECT_EQ(test, scheme.core_filters_default_reject, false);
	KUNIT_EXPECT_EQ(test, scheme.ops_filters_default_reject, true);
}

static struct kunit_case damon_test_cases[] = {
	KUNIT_CASE(damon_test_target),
	KUNIT_CASE(damon_test_regions),
	KUNIT_CASE(damon_test_aggregate),
	KUNIT_CASE(damon_test_split_at),
	KUNIT_CASE(damon_test_merge_two),
	KUNIT_CASE(damon_test_merge_two_samples),
	KUNIT_CASE(damon_test_adapt_samplings),
	KUNIT_CASE(damon_test_regions_similar),
	KUNIT_CASE(damon_test_region_hotness),
	KUNIT_CASE(damon_test_sampling_schedule),
	KUNIT_CASE(damon_test_time_stat),
	KUNIT_CASE(damon_test_merge_regions_of),
	KUNIT_CASE(damon_test_split_regions_of),
	KUNIT_CASE(damon_test_min_sz_region_2mb),
	KUNIT_CASE(damon_test_telescope_pieces),
	KUNIT_CASE(damon_test_range_level),
	KUNIT_CASE(damon_test_ops_registration),
	KUNIT_CASE(damon_test_set_regions),
	KUNIT_CASE(damon_test_nr_accesses_to_accesses_bp),
	KUNIT_CASE(damon_test_update_monitoring_result),
	KUNIT_CASE(damon_test_set_attrs),
	KUNIT_CASE(damon_test_moving_sum),
	KUNIT_CASE(damos_test_new_filter),
	KUNIT_CASE(damos_test_commit_filter),
	KUNIT_CASE(damos_test_filter_out),
	KUNIT_CASE(damon_test_feed_loop_next_input),
	KUNIT_CASE(damon_test_set_filters_default_reject),
	{},
};

static struct kunit_suite damon_test_suite = {
	.name = "damon",
	.test_cases = damon_test_cases,
};
kunit_test_suite(damon_test_suite);

#endif /* _DAMON_CORE_TEST_H */

#endif	/* CONFIG_DAMON_KUNIT_TEST */
