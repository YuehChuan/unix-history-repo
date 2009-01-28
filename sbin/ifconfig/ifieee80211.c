/*
 * Copyright 2001 The Aerospace Corporation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of The Aerospace Corporation may not be used to endorse or
 *    promote products derived from this software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AEROSPACE CORPORATION ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AEROSPACE CORPORATION BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*-
 * Copyright (c) 1997, 1998, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_media.h>
#include <net/route.h>

#include <net80211/ieee80211_ioctl.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stddef.h>		/* NB: for offsetof */

#include "ifconfig.h"
#include "regdomain.h"

#ifndef IEEE80211_FIXED_RATE_NONE
#define	IEEE80211_FIXED_RATE_NONE	0xff
#endif

#define	REQ_ECM		0x01000000	/* enable if ECM set */
#define	REQ_OUTDOOR	0x02000000	/* enable for outdoor operation */
#define	REQ_FLAGS	0xff000000	/* private flags, don't pass to os */

/* XXX need these publicly defined or similar */
#ifndef IEEE80211_NODE_AUTH
#define	IEEE80211_NODE_AUTH	0x0001		/* authorized for data */
#define	IEEE80211_NODE_QOS	0x0002		/* QoS enabled */
#define	IEEE80211_NODE_ERP	0x0004		/* ERP enabled */
#define	IEEE80211_NODE_PWR_MGT	0x0010		/* power save mode enabled */
#define	IEEE80211_NODE_HT	0x0040		/* HT enabled */
#define	IEEE80211_NODE_HTCOMPAT	0x0080		/* HT setup w/ vendor OUI's */
#define	IEEE80211_NODE_WPS	0x0100		/* WPS association */
#define	IEEE80211_NODE_TSN	0x0200		/* TSN association */
#define	IEEE80211_NODE_AMPDU_RX	0x0400		/* AMPDU rx enabled */
#define	IEEE80211_NODE_AMPDU_TX	0x0800		/* AMPDU tx enabled */
#define	IEEE80211_NODE_MIMO_PS	0x1000		/* MIMO power save enabled */
#define	IEEE80211_NODE_MIMO_RTS	0x2000		/* send RTS in MIMO PS */
#define	IEEE80211_NODE_RIFS	0x4000		/* RIFS enabled */
#endif

#define	MAXCHAN	1536		/* max 1.5K channels */

#define	MAXCOL	78
static	int col;
static	char spacer;

static void LINE_INIT(char c);
static void LINE_BREAK(void);
static void LINE_CHECK(const char *fmt, ...);

static const char *modename[] = {
	"auto", "11a", "11b", "11g", "fh", "turboA", "turboG",
	"sturbo", "11na", "11ng"
};

static void set80211(int s, int type, int val, int len, void *data);
static int get80211(int s, int type, void *data, int len);
static int get80211len(int s, int type, void *data, int len, int *plen);
static int get80211val(int s, int type, int *val);
static const char *get_string(const char *val, const char *sep,
    u_int8_t *buf, int *lenp);
static void print_string(const u_int8_t *buf, int len);
static void print_regdomain(const struct ieee80211_regdomain *, int);
static void print_channels(int, const struct ieee80211req_chaninfo *,
    int allchans, int verbose);
static void regdomain_makechannels(struct ieee80211_regdomain_req *,
    const struct ieee80211_devcaps_req *);

static struct ieee80211req_chaninfo *chaninfo;
static struct ieee80211_regdomain regdomain;
static int gotregdomain = 0;
static struct ieee80211_roamparams_req roamparams;
static int gotroam = 0;
static struct ieee80211_txparams_req txparams;
static int gottxparams = 0;
static struct ieee80211_channel curchan;
static int gotcurchan = 0;
static struct ifmediareq *ifmr;
static int htconf = 0;
static	int gothtconf = 0;

static void
gethtconf(int s)
{
	if (gothtconf)
		return;
	if (get80211val(s, IEEE80211_IOC_HTCONF, &htconf) < 0)
		warn("unable to get HT configuration information");
	gothtconf = 1;
}

/*
 * Collect channel info from the kernel.  We use this (mostly)
 * to handle mapping between frequency and IEEE channel number.
 */
static void
getchaninfo(int s)
{
	if (chaninfo != NULL)
		return;
	chaninfo = malloc(IEEE80211_CHANINFO_SIZE(MAXCHAN));
	if (chaninfo == NULL)
		errx(1, "no space for channel list");
	if (get80211(s, IEEE80211_IOC_CHANINFO, chaninfo,
	    IEEE80211_CHANINFO_SIZE(MAXCHAN)) < 0)
		err(1, "unable to get channel information");
	ifmr = ifmedia_getstate(s);
	gethtconf(s);
}

static struct regdata *
getregdata(void)
{
	static struct regdata *rdp = NULL;
	if (rdp == NULL) {
		rdp = lib80211_alloc_regdata();
		if (rdp == NULL)
			errx(-1, "missing or corrupted regdomain database");
	}
	return rdp;
}

/*
 * Given the channel at index i with attributes from,
 * check if there is a channel with attributes to in
 * the channel table.  With suitable attributes this
 * allows the caller to look for promotion; e.g. from
 * 11b > 11g.
 */
static int
canpromote(int i, int from, int to)
{
	const struct ieee80211_channel *fc = &chaninfo->ic_chans[i];
	int j;

	if ((fc->ic_flags & from) != from)
		return i;
	/* NB: quick check exploiting ordering of chans w/ same frequency */
	if (i+1 < chaninfo->ic_nchans &&
	    chaninfo->ic_chans[i+1].ic_freq == fc->ic_freq &&
	    (chaninfo->ic_chans[i+1].ic_flags & to) == to)
		return i+1;
	/* brute force search in case channel list is not ordered */
	for (j = 0; j < chaninfo->ic_nchans; j++) {
		const struct ieee80211_channel *tc = &chaninfo->ic_chans[j];
		if (j != i &&
		    tc->ic_freq == fc->ic_freq && (tc->ic_flags & to) == to)
		return j;
	}
	return i;
}

/*
 * Handle channel promotion.  When a channel is specified with
 * only a frequency we want to promote it to the ``best'' channel
 * available.  The channel list has separate entries for 11b, 11g,
 * 11a, and 11n[ga] channels so specifying a frequency w/o any
 * attributes requires we upgrade, e.g. from 11b -> 11g.  This
 * gets complicated when the channel is specified on the same
 * command line with a media request that constrains the available
 * channe list (e.g. mode 11a); we want to honor that to avoid
 * confusing behaviour.
 */
static int
promote(int i)
{
	/*
	 * Query the current mode of the interface in case it's
	 * constrained (e.g. to 11a).  We must do this carefully
	 * as there may be a pending ifmedia request in which case
	 * asking the kernel will give us the wrong answer.  This
	 * is an unfortunate side-effect of the way ifconfig is
	 * structure for modularity (yech).
	 *
	 * NB: ifmr is actually setup in getchaninfo (above); we
	 *     assume it's called coincident with to this call so
	 *     we have a ``current setting''; otherwise we must pass
	 *     the socket descriptor down to here so we can make
	 *     the ifmedia_getstate call ourselves.
	 */
	int chanmode = ifmr != NULL ? IFM_MODE(ifmr->ifm_current) : IFM_AUTO;

	/* when ambiguous promote to ``best'' */
	/* NB: we abitrarily pick HT40+ over HT40- */
	if (chanmode != IFM_IEEE80211_11B)
		i = canpromote(i, IEEE80211_CHAN_B, IEEE80211_CHAN_G);
	if (chanmode != IFM_IEEE80211_11G && (htconf & 1)) {
		i = canpromote(i, IEEE80211_CHAN_G,
			IEEE80211_CHAN_G | IEEE80211_CHAN_HT20);
		if (htconf & 2) {
			i = canpromote(i, IEEE80211_CHAN_G,
				IEEE80211_CHAN_G | IEEE80211_CHAN_HT40D);
			i = canpromote(i, IEEE80211_CHAN_G,
				IEEE80211_CHAN_G | IEEE80211_CHAN_HT40U);
		}
	}
	if (chanmode != IFM_IEEE80211_11A && (htconf & 1)) {
		i = canpromote(i, IEEE80211_CHAN_A,
			IEEE80211_CHAN_A | IEEE80211_CHAN_HT20);
		if (htconf & 2) {
			i = canpromote(i, IEEE80211_CHAN_A,
				IEEE80211_CHAN_A | IEEE80211_CHAN_HT40D);
			i = canpromote(i, IEEE80211_CHAN_A,
				IEEE80211_CHAN_A | IEEE80211_CHAN_HT40U);
		}
	}
	return i;
}

static void
mapfreq(struct ieee80211_channel *chan, int freq, int flags)
{
	int i;

	for (i = 0; i < chaninfo->ic_nchans; i++) {
		const struct ieee80211_channel *c = &chaninfo->ic_chans[i];

		if (c->ic_freq == freq && (c->ic_flags & flags) == flags) {
			if (flags == 0) {
				/* when ambiguous promote to ``best'' */
				c = &chaninfo->ic_chans[promote(i)];
			}
			*chan = *c;
			return;
		}
	}
	errx(1, "unknown/undefined frequency %u/0x%x", freq, flags);
}

static void
mapchan(struct ieee80211_channel *chan, int ieee, int flags)
{
	int i;

	for (i = 0; i < chaninfo->ic_nchans; i++) {
		const struct ieee80211_channel *c = &chaninfo->ic_chans[i];

		if (c->ic_ieee == ieee && (c->ic_flags & flags) == flags) {
			if (flags == 0) {
				/* when ambiguous promote to ``best'' */
				c = &chaninfo->ic_chans[promote(i)];
			}
			*chan = *c;
			return;
		}
	}
	errx(1, "unknown/undefined channel number %d flags 0x%x", ieee, flags);
}

static const struct ieee80211_channel *
getcurchan(int s)
{
	if (gotcurchan)
		return &curchan;
	if (get80211(s, IEEE80211_IOC_CURCHAN, &curchan, sizeof(curchan)) < 0) {
		int val;
		/* fall back to legacy ioctl */
		if (get80211val(s, IEEE80211_IOC_CHANNEL, &val) < 0)
			err(-1, "cannot figure out current channel");
		getchaninfo(s);
		mapchan(&curchan, val, 0);
	}
	gotcurchan = 1;
	return &curchan;
}

static enum ieee80211_phymode
chan2mode(const struct ieee80211_channel *c)
{
	if (IEEE80211_IS_CHAN_HTA(c))
		return IEEE80211_MODE_11NA;
	if (IEEE80211_IS_CHAN_HTG(c))
		return IEEE80211_MODE_11NG;
	if (IEEE80211_IS_CHAN_108A(c))
		return IEEE80211_MODE_TURBO_A;
	if (IEEE80211_IS_CHAN_108G(c))
		return IEEE80211_MODE_TURBO_G;
	if (IEEE80211_IS_CHAN_ST(c))
		return IEEE80211_MODE_STURBO_A;
	if (IEEE80211_IS_CHAN_FHSS(c))
		return IEEE80211_MODE_FH;
	if (IEEE80211_IS_CHAN_A(c))
		return IEEE80211_MODE_11A;
	if (IEEE80211_IS_CHAN_ANYG(c))
		return IEEE80211_MODE_11G;
	if (IEEE80211_IS_CHAN_B(c))
		return IEEE80211_MODE_11B;
	return IEEE80211_MODE_AUTO;
}

static void
getroam(int s)
{
	if (gotroam)
		return;
	if (get80211(s, IEEE80211_IOC_ROAM,
	    &roamparams, sizeof(roamparams)) < 0)
		err(1, "unable to get roaming parameters");
	gotroam = 1;
}

static void
setroam_cb(int s, void *arg)
{
	struct ieee80211_roamparams_req *roam = arg;
	set80211(s, IEEE80211_IOC_ROAM, 0, sizeof(*roam), roam);
}

static void
gettxparams(int s)
{
	if (gottxparams)
		return;
	if (get80211(s, IEEE80211_IOC_TXPARAMS,
	    &txparams, sizeof(txparams)) < 0)
		err(1, "unable to get transmit parameters");
	gottxparams = 1;
}

static void
settxparams_cb(int s, void *arg)
{
	struct ieee80211_txparams_req *txp = arg;
	set80211(s, IEEE80211_IOC_TXPARAMS, 0, sizeof(*txp), txp);
}

static void
getregdomain(int s)
{
	if (gotregdomain)
		return;
	if (get80211(s, IEEE80211_IOC_REGDOMAIN,
	    &regdomain, sizeof(regdomain)) < 0)
		err(1, "unable to get regulatory domain info");
	gotregdomain = 1;
}

static void
getdevcaps(int s, struct ieee80211_devcaps_req *dc)
{
	if (get80211(s, IEEE80211_IOC_DEVCAPS, dc,
	    IEEE80211_DEVCAPS_SPACE(dc)) < 0)
		err(1, "unable to get device capabilities");
}

static void
setregdomain_cb(int s, void *arg)
{
	struct ieee80211_regdomain_req *req;
	struct ieee80211_regdomain *rd = arg;
	struct ieee80211_devcaps_req *dc;
	struct regdata *rdp = getregdata();

	if (rd->country != NO_COUNTRY) {
		const struct country *cc;
		/*
		 * Check current country seting to make sure it's
		 * compatible with the new regdomain.  If not, then
		 * override it with any default country for this
		 * SKU.  If we cannot arrange a match, then abort.
		 */
		cc = lib80211_country_findbycc(rdp, rd->country);
		if (cc == NULL)
			errx(1, "unknown ISO country code %d", rd->country);
		if (cc->rd->sku != rd->regdomain) {
			const struct regdomain *rp;
			/*
			 * Check if country is incompatible with regdomain.
			 * To enable multiple regdomains for a country code
			 * we permit a mismatch between the regdomain and
			 * the country's associated regdomain when the
			 * regdomain is setup w/o a default country.  For
			 * example, US is bound to the FCC regdomain but
			 * we allow US to be combined with FCC3 because FCC3
			 * has not default country.  This allows bogus
			 * combinations like FCC3+DK which are resolved when
			 * constructing the channel list by deferring to the
			 * regdomain to construct the channel list.
			 */
			rp = lib80211_regdomain_findbysku(rdp, rd->regdomain);
			if (rp == NULL)
				errx(1, "country %s (%s) is not usable with "
				    "regdomain %d", cc->isoname, cc->name,
				    rd->regdomain);
			else if (rp->cc != NULL && rp->cc != cc)
				errx(1, "country %s (%s) is not usable with "
				   "regdomain %s", cc->isoname, cc->name,
				   rp->name);
		}
	}
	/*
	 * Fetch the device capabilities and calculate the
	 * full set of netbands for which we request a new
	 * channel list be constructed.  Once that's done we
	 * push the regdomain info + channel list to the kernel.
	 */
	dc = malloc(IEEE80211_DEVCAPS_SIZE(MAXCHAN));
	if (dc == NULL)
		errx(1, "no space for device capabilities");
	dc->dc_chaninfo.ic_nchans = MAXCHAN;
	getdevcaps(s, dc);
#if 0
	if (verbose) {
		printf("drivercaps: 0x%x\n", dc->dc_drivercaps);
		printf("cryptocaps: 0x%x\n", dc->dc_cryptocaps);
		printf("htcaps    : 0x%x\n", dc->dc_htcaps);
		memcpy(chaninfo, &dc->dc_chaninfo,
		    IEEE80211_CHANINFO_SPACE(&dc->dc_chaninfo));
		print_channels(s, &dc->dc_chaninfo, 1/*allchans*/, 1/*verbose*/);
	}
#endif
	req = malloc(IEEE80211_REGDOMAIN_SIZE(dc->dc_chaninfo.ic_nchans));
	if (req == NULL)
		errx(1, "no space for regdomain request");
	req->rd = *rd;
	regdomain_makechannels(req, dc);
	if (verbose) {
		LINE_INIT(':');
		print_regdomain(rd, 1/*verbose*/);
		LINE_BREAK();
		/* blech, reallocate channel list for new data */
		if (chaninfo != NULL)
			free(chaninfo);
		chaninfo = malloc(IEEE80211_CHANINFO_SPACE(&req->chaninfo));
		if (chaninfo == NULL)
			errx(1, "no space for channel list");
		memcpy(chaninfo, &req->chaninfo,
		    IEEE80211_CHANINFO_SPACE(&req->chaninfo));
		print_channels(s, &req->chaninfo, 1/*allchans*/, 1/*verbose*/);
	}
	if (req->chaninfo.ic_nchans == 0)
		errx(1, "no channels calculated");
	set80211(s, IEEE80211_IOC_REGDOMAIN, 0,
	    IEEE80211_REGDOMAIN_SPACE(req), req);
	free(req);
	free(dc);
}

static int
ieee80211_mhz2ieee(int freq, int flags)
{
	struct ieee80211_channel chan;
	mapfreq(&chan, freq, flags);
	return chan.ic_ieee;
}

static int
isanyarg(const char *arg)
{
	return (strncmp(arg, "-", 1) == 0 ||
	    strncasecmp(arg, "any", 3) == 0 || strncasecmp(arg, "off", 3) == 0);
}

static void
set80211ssid(const char *val, int d, int s, const struct afswtch *rafp)
{
	int		ssid;
	int		len;
	u_int8_t	data[IEEE80211_NWID_LEN];

	ssid = 0;
	len = strlen(val);
	if (len > 2 && isdigit((int)val[0]) && val[1] == ':') {
		ssid = atoi(val)-1;
		val += 2;
	}

	bzero(data, sizeof(data));
	len = sizeof(data);
	if (get_string(val, NULL, data, &len) == NULL)
		exit(1);

	set80211(s, IEEE80211_IOC_SSID, ssid, len, data);
}

static void
set80211stationname(const char *val, int d, int s, const struct afswtch *rafp)
{
	int			len;
	u_int8_t		data[33];

	bzero(data, sizeof(data));
	len = sizeof(data);
	get_string(val, NULL, data, &len);

	set80211(s, IEEE80211_IOC_STATIONNAME, 0, len, data);
}

/*
 * Parse a channel specification for attributes/flags.
 * The syntax is:
 *	freq/xx		channel width (5,10,20,40,40+,40-)
 *	freq:mode	channel mode (a,b,g,h,n,t,s,d)
 *
 * These can be combined in either order; e.g. 2437:ng/40.
 * Modes are case insensitive.
 *
 * The result is not validated here; it's assumed to be
 * checked against the channel table fetched from the kernel.
 */ 
static int
getchannelflags(const char *val, int freq)
{
#define	_CHAN_HT	0x80000000
	const char *cp;
	int flags;

	flags = 0;

	cp = strchr(val, ':');
	if (cp != NULL) {
		for (cp++; isalpha((int) *cp); cp++) {
			/* accept mixed case */
			int c = *cp;
			if (isupper(c))
				c = tolower(c);
			switch (c) {
			case 'a':		/* 802.11a */
				flags |= IEEE80211_CHAN_A;
				break;
			case 'b':		/* 802.11b */
				flags |= IEEE80211_CHAN_B;
				break;
			case 'g':		/* 802.11g */
				flags |= IEEE80211_CHAN_G;
				break;
			case 'h':		/* ht = 802.11n */
			case 'n':		/* 802.11n */
				flags |= _CHAN_HT;	/* NB: private */
				break;
			case 'd':		/* dt = Atheros Dynamic Turbo */
				flags |= IEEE80211_CHAN_TURBO;
				break;
			case 't':		/* ht, dt, st, t */
				/* dt and unadorned t specify Dynamic Turbo */
				if ((flags & (IEEE80211_CHAN_STURBO|_CHAN_HT)) == 0)
					flags |= IEEE80211_CHAN_TURBO;
				break;
			case 's':		/* st = Atheros Static Turbo */
				flags |= IEEE80211_CHAN_STURBO;
				break;
			default:
				errx(-1, "%s: Invalid channel attribute %c\n",
				    val, *cp);
			}
		}
	}
	cp = strchr(val, '/');
	if (cp != NULL) {
		char *ep;
		u_long cw = strtoul(cp+1, &ep, 10);

		switch (cw) {
		case 5:
			flags |= IEEE80211_CHAN_QUARTER;
			break;
		case 10:
			flags |= IEEE80211_CHAN_HALF;
			break;
		case 20:
			/* NB: this may be removed below */
			flags |= IEEE80211_CHAN_HT20;
			break;
		case 40:
			if (ep != NULL && *ep == '+')
				flags |= IEEE80211_CHAN_HT40U;
			else if (ep != NULL && *ep == '-')
				flags |= IEEE80211_CHAN_HT40D;
			break;
		default:
			errx(-1, "%s: Invalid channel width\n", val);
		}
	}
	/*
	 * Cleanup specifications.
	 */ 
	if ((flags & _CHAN_HT) == 0) {
		/*
		 * If user specified freq/20 or freq/40 quietly remove
		 * HT cw attributes depending on channel use.  To give
		 * an explicit 20/40 width for an HT channel you must
		 * indicate it is an HT channel since all HT channels
		 * are also usable for legacy operation; e.g. freq:n/40.
		 */
		flags &= ~IEEE80211_CHAN_HT;
	} else {
		/*
		 * Remove private indicator that this is an HT channel
		 * and if no explicit channel width has been given
		 * provide the default settings.
		 */
		flags &= ~_CHAN_HT;
		if ((flags & IEEE80211_CHAN_HT) == 0) {
			struct ieee80211_channel chan;
			/*
			 * Consult the channel list to see if we can use
			 * HT40+ or HT40- (if both the map routines choose).
			 */
			if (freq > 255)
				mapfreq(&chan, freq, 0);
			else
				mapchan(&chan, freq, 0);
			flags |= (chan.ic_flags & IEEE80211_CHAN_HT);
		}
	}
	return flags;
#undef _CHAN_HT
}

static void
getchannel(int s, struct ieee80211_channel *chan, const char *val)
{
	int v, flags;
	char *eptr;

	memset(chan, 0, sizeof(*chan));
	if (isanyarg(val)) {
		chan->ic_freq = IEEE80211_CHAN_ANY;
		return;
	}
	getchaninfo(s);
	errno = 0;
	v = strtol(val, &eptr, 10);
	if (val[0] == '\0' || val == eptr || errno == ERANGE ||
	    /* channel may be suffixed with nothing, :flag, or /width */
	    (eptr[0] != '\0' && eptr[0] != ':' && eptr[0] != '/'))
		errx(1, "invalid channel specification%s",
		    errno == ERANGE ? " (out of range)" : "");
	flags = getchannelflags(val, v);
	if (v > 255) {		/* treat as frequency */
		mapfreq(chan, v, flags);
	} else {
		mapchan(chan, v, flags);
	}
}

static void
set80211channel(const char *val, int d, int s, const struct afswtch *rafp)
{
	struct ieee80211_channel chan;

	getchannel(s, &chan, val);
	set80211(s, IEEE80211_IOC_CURCHAN, 0, sizeof(chan), &chan);
}

static void
set80211chanswitch(const char *val, int d, int s, const struct afswtch *rafp)
{
	struct ieee80211_chanswitch_req csr;

	getchannel(s, &csr.csa_chan, val);
	csr.csa_mode = 1;
	csr.csa_count = 5;
	set80211(s, IEEE80211_IOC_CHANSWITCH, 0, sizeof(csr), &csr);
}

static void
set80211authmode(const char *val, int d, int s, const struct afswtch *rafp)
{
	int	mode;

	if (strcasecmp(val, "none") == 0) {
		mode = IEEE80211_AUTH_NONE;
	} else if (strcasecmp(val, "open") == 0) {
		mode = IEEE80211_AUTH_OPEN;
	} else if (strcasecmp(val, "shared") == 0) {
		mode = IEEE80211_AUTH_SHARED;
	} else if (strcasecmp(val, "8021x") == 0) {
		mode = IEEE80211_AUTH_8021X;
	} else if (strcasecmp(val, "wpa") == 0) {
		mode = IEEE80211_AUTH_WPA;
	} else {
		errx(1, "unknown authmode");
	}

	set80211(s, IEEE80211_IOC_AUTHMODE, mode, 0, NULL);
}

static void
set80211powersavemode(const char *val, int d, int s, const struct afswtch *rafp)
{
	int	mode;

	if (strcasecmp(val, "off") == 0) {
		mode = IEEE80211_POWERSAVE_OFF;
	} else if (strcasecmp(val, "on") == 0) {
		mode = IEEE80211_POWERSAVE_ON;
	} else if (strcasecmp(val, "cam") == 0) {
		mode = IEEE80211_POWERSAVE_CAM;
	} else if (strcasecmp(val, "psp") == 0) {
		mode = IEEE80211_POWERSAVE_PSP;
	} else if (strcasecmp(val, "psp-cam") == 0) {
		mode = IEEE80211_POWERSAVE_PSP_CAM;
	} else {
		errx(1, "unknown powersavemode");
	}

	set80211(s, IEEE80211_IOC_POWERSAVE, mode, 0, NULL);
}

static void
set80211powersave(const char *val, int d, int s, const struct afswtch *rafp)
{
	if (d == 0)
		set80211(s, IEEE80211_IOC_POWERSAVE, IEEE80211_POWERSAVE_OFF,
		    0, NULL);
	else
		set80211(s, IEEE80211_IOC_POWERSAVE, IEEE80211_POWERSAVE_ON,
		    0, NULL);
}

static void
set80211powersavesleep(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_POWERSAVESLEEP, atoi(val), 0, NULL);
}

static void
set80211wepmode(const char *val, int d, int s, const struct afswtch *rafp)
{
	int	mode;

	if (strcasecmp(val, "off") == 0) {
		mode = IEEE80211_WEP_OFF;
	} else if (strcasecmp(val, "on") == 0) {
		mode = IEEE80211_WEP_ON;
	} else if (strcasecmp(val, "mixed") == 0) {
		mode = IEEE80211_WEP_MIXED;
	} else {
		errx(1, "unknown wep mode");
	}

	set80211(s, IEEE80211_IOC_WEP, mode, 0, NULL);
}

static void
set80211wep(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_WEP, d, 0, NULL);
}

static int
isundefarg(const char *arg)
{
	return (strcmp(arg, "-") == 0 || strncasecmp(arg, "undef", 5) == 0);
}

static void
set80211weptxkey(const char *val, int d, int s, const struct afswtch *rafp)
{
	if (isundefarg(val))
		set80211(s, IEEE80211_IOC_WEPTXKEY, IEEE80211_KEYIX_NONE, 0, NULL);
	else
		set80211(s, IEEE80211_IOC_WEPTXKEY, atoi(val)-1, 0, NULL);
}

static void
set80211wepkey(const char *val, int d, int s, const struct afswtch *rafp)
{
	int		key = 0;
	int		len;
	u_int8_t	data[IEEE80211_KEYBUF_SIZE];

	if (isdigit((int)val[0]) && val[1] == ':') {
		key = atoi(val)-1;
		val += 2;
	}

	bzero(data, sizeof(data));
	len = sizeof(data);
	get_string(val, NULL, data, &len);

	set80211(s, IEEE80211_IOC_WEPKEY, key, len, data);
}

/*
 * This function is purely a NetBSD compatability interface.  The NetBSD
 * interface is too inflexible, but it's there so we'll support it since
 * it's not all that hard.
 */
static void
set80211nwkey(const char *val, int d, int s, const struct afswtch *rafp)
{
	int		txkey;
	int		i, len;
	u_int8_t	data[IEEE80211_KEYBUF_SIZE];

	set80211(s, IEEE80211_IOC_WEP, IEEE80211_WEP_ON, 0, NULL);

	if (isdigit((int)val[0]) && val[1] == ':') {
		txkey = val[0]-'0'-1;
		val += 2;

		for (i = 0; i < 4; i++) {
			bzero(data, sizeof(data));
			len = sizeof(data);
			val = get_string(val, ",", data, &len);
			if (val == NULL)
				exit(1);

			set80211(s, IEEE80211_IOC_WEPKEY, i, len, data);
		}
	} else {
		bzero(data, sizeof(data));
		len = sizeof(data);
		get_string(val, NULL, data, &len);
		txkey = 0;

		set80211(s, IEEE80211_IOC_WEPKEY, 0, len, data);

		bzero(data, sizeof(data));
		for (i = 1; i < 4; i++)
			set80211(s, IEEE80211_IOC_WEPKEY, i, 0, data);
	}

	set80211(s, IEEE80211_IOC_WEPTXKEY, txkey, 0, NULL);
}

static void
set80211rtsthreshold(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_RTSTHRESHOLD,
		isundefarg(val) ? IEEE80211_RTS_MAX : atoi(val), 0, NULL);
}

static void
set80211protmode(const char *val, int d, int s, const struct afswtch *rafp)
{
	int	mode;

	if (strcasecmp(val, "off") == 0) {
		mode = IEEE80211_PROTMODE_OFF;
	} else if (strcasecmp(val, "cts") == 0) {
		mode = IEEE80211_PROTMODE_CTS;
	} else if (strncasecmp(val, "rtscts", 3) == 0) {
		mode = IEEE80211_PROTMODE_RTSCTS;
	} else {
		errx(1, "unknown protection mode");
	}

	set80211(s, IEEE80211_IOC_PROTMODE, mode, 0, NULL);
}

static void
set80211htprotmode(const char *val, int d, int s, const struct afswtch *rafp)
{
	int	mode;

	if (strcasecmp(val, "off") == 0) {
		mode = IEEE80211_PROTMODE_OFF;
	} else if (strncasecmp(val, "rts", 3) == 0) {
		mode = IEEE80211_PROTMODE_RTSCTS;
	} else {
		errx(1, "unknown protection mode");
	}

	set80211(s, IEEE80211_IOC_HTPROTMODE, mode, 0, NULL);
}

static void
set80211txpower(const char *val, int d, int s, const struct afswtch *rafp)
{
	double v = atof(val);
	int txpow;

	txpow = (int) (2*v);
	if (txpow != 2*v)
		errx(-1, "invalid tx power (must be .5 dBm units)");
	set80211(s, IEEE80211_IOC_TXPOWER, txpow, 0, NULL);
}

#define	IEEE80211_ROAMING_DEVICE	0
#define	IEEE80211_ROAMING_AUTO		1
#define	IEEE80211_ROAMING_MANUAL	2

static void
set80211roaming(const char *val, int d, int s, const struct afswtch *rafp)
{
	int mode;

	if (strcasecmp(val, "device") == 0) {
		mode = IEEE80211_ROAMING_DEVICE;
	} else if (strcasecmp(val, "auto") == 0) {
		mode = IEEE80211_ROAMING_AUTO;
	} else if (strcasecmp(val, "manual") == 0) {
		mode = IEEE80211_ROAMING_MANUAL;
	} else {
		errx(1, "unknown roaming mode");
	}
	set80211(s, IEEE80211_IOC_ROAMING, mode, 0, NULL);
}

static void
set80211wme(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_WME, d, 0, NULL);
}

static void
set80211hidessid(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_HIDESSID, d, 0, NULL);
}

static void
set80211apbridge(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_APBRIDGE, d, 0, NULL);
}

static void
set80211fastframes(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_FF, d, 0, NULL);
}

static void
set80211dturbo(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_TURBOP, d, 0, NULL);
}

static void
set80211chanlist(const char *val, int d, int s, const struct afswtch *rafp)
{
	struct ieee80211req_chanlist chanlist;
	char *temp, *cp, *tp;

	temp = malloc(strlen(val) + 1);
	if (temp == NULL)
		errx(1, "malloc failed");
	strcpy(temp, val);
	memset(&chanlist, 0, sizeof(chanlist));
	cp = temp;
	for (;;) {
		int first, last, f, c;

		tp = strchr(cp, ',');
		if (tp != NULL)
			*tp++ = '\0';
		switch (sscanf(cp, "%u-%u", &first, &last)) {
		case 1:
			if (first > IEEE80211_CHAN_MAX)
				errx(-1, "channel %u out of range, max %zu",
					first, IEEE80211_CHAN_MAX);
			setbit(chanlist.ic_channels, first);
			break;
		case 2:
			if (first > IEEE80211_CHAN_MAX)
				errx(-1, "channel %u out of range, max %zu",
					first, IEEE80211_CHAN_MAX);
			if (last > IEEE80211_CHAN_MAX)
				errx(-1, "channel %u out of range, max %zu",
					last, IEEE80211_CHAN_MAX);
			if (first > last)
				errx(-1, "void channel range, %u > %u",
					first, last);
			for (f = first; f <= last; f++)
				setbit(chanlist.ic_channels, f);
			break;
		}
		if (tp == NULL)
			break;
		c = *tp;
		while (isspace(c))
			tp++;
		if (!isdigit(c))
			break;
		cp = tp;
	}
	set80211(s, IEEE80211_IOC_CHANLIST, 0, sizeof(chanlist), &chanlist);
}

static void
set80211bssid(const char *val, int d, int s, const struct afswtch *rafp)
{

	if (!isanyarg(val)) {
		char *temp;
		struct sockaddr_dl sdl;

		temp = malloc(strlen(val) + 2); /* ':' and '\0' */
		if (temp == NULL)
			errx(1, "malloc failed");
		temp[0] = ':';
		strcpy(temp + 1, val);
		sdl.sdl_len = sizeof(sdl);
		link_addr(temp, &sdl);
		free(temp);
		if (sdl.sdl_alen != IEEE80211_ADDR_LEN)
			errx(1, "malformed link-level address");
		set80211(s, IEEE80211_IOC_BSSID, 0,
			IEEE80211_ADDR_LEN, LLADDR(&sdl));
	} else {
		uint8_t zerobssid[IEEE80211_ADDR_LEN];
		memset(zerobssid, 0, sizeof(zerobssid));
		set80211(s, IEEE80211_IOC_BSSID, 0,
			IEEE80211_ADDR_LEN, zerobssid);
	}
}

static int
getac(const char *ac)
{
	if (strcasecmp(ac, "ac_be") == 0 || strcasecmp(ac, "be") == 0)
		return WME_AC_BE;
	if (strcasecmp(ac, "ac_bk") == 0 || strcasecmp(ac, "bk") == 0)
		return WME_AC_BK;
	if (strcasecmp(ac, "ac_vi") == 0 || strcasecmp(ac, "vi") == 0)
		return WME_AC_VI;
	if (strcasecmp(ac, "ac_vo") == 0 || strcasecmp(ac, "vo") == 0)
		return WME_AC_VO;
	errx(1, "unknown wme access class %s", ac);
}

static
DECL_CMD_FUNC2(set80211cwmin, ac, val)
{
	set80211(s, IEEE80211_IOC_WME_CWMIN, atoi(val), getac(ac), NULL);
}

static
DECL_CMD_FUNC2(set80211cwmax, ac, val)
{
	set80211(s, IEEE80211_IOC_WME_CWMAX, atoi(val), getac(ac), NULL);
}

static
DECL_CMD_FUNC2(set80211aifs, ac, val)
{
	set80211(s, IEEE80211_IOC_WME_AIFS, atoi(val), getac(ac), NULL);
}

static
DECL_CMD_FUNC2(set80211txoplimit, ac, val)
{
	set80211(s, IEEE80211_IOC_WME_TXOPLIMIT, atoi(val), getac(ac), NULL);
}

static
DECL_CMD_FUNC(set80211acm, ac, d)
{
	set80211(s, IEEE80211_IOC_WME_ACM, 1, getac(ac), NULL);
}
static
DECL_CMD_FUNC(set80211noacm, ac, d)
{
	set80211(s, IEEE80211_IOC_WME_ACM, 0, getac(ac), NULL);
}

static
DECL_CMD_FUNC(set80211ackpolicy, ac, d)
{
	set80211(s, IEEE80211_IOC_WME_ACKPOLICY, 1, getac(ac), NULL);
}
static
DECL_CMD_FUNC(set80211noackpolicy, ac, d)
{
	set80211(s, IEEE80211_IOC_WME_ACKPOLICY, 0, getac(ac), NULL);
}

static
DECL_CMD_FUNC2(set80211bsscwmin, ac, val)
{
	set80211(s, IEEE80211_IOC_WME_CWMIN, atoi(val),
		getac(ac)|IEEE80211_WMEPARAM_BSS, NULL);
}

static
DECL_CMD_FUNC2(set80211bsscwmax, ac, val)
{
	set80211(s, IEEE80211_IOC_WME_CWMAX, atoi(val),
		getac(ac)|IEEE80211_WMEPARAM_BSS, NULL);
}

static
DECL_CMD_FUNC2(set80211bssaifs, ac, val)
{
	set80211(s, IEEE80211_IOC_WME_AIFS, atoi(val),
		getac(ac)|IEEE80211_WMEPARAM_BSS, NULL);
}

static
DECL_CMD_FUNC2(set80211bsstxoplimit, ac, val)
{
	set80211(s, IEEE80211_IOC_WME_TXOPLIMIT, atoi(val),
		getac(ac)|IEEE80211_WMEPARAM_BSS, NULL);
}

static
DECL_CMD_FUNC(set80211dtimperiod, val, d)
{
	set80211(s, IEEE80211_IOC_DTIM_PERIOD, atoi(val), 0, NULL);
}

static
DECL_CMD_FUNC(set80211bintval, val, d)
{
	set80211(s, IEEE80211_IOC_BEACON_INTERVAL, atoi(val), 0, NULL);
}

static void
set80211macmac(int s, int op, const char *val)
{
	char *temp;
	struct sockaddr_dl sdl;

	temp = malloc(strlen(val) + 2); /* ':' and '\0' */
	if (temp == NULL)
		errx(1, "malloc failed");
	temp[0] = ':';
	strcpy(temp + 1, val);
	sdl.sdl_len = sizeof(sdl);
	link_addr(temp, &sdl);
	free(temp);
	if (sdl.sdl_alen != IEEE80211_ADDR_LEN)
		errx(1, "malformed link-level address");
	set80211(s, op, 0, IEEE80211_ADDR_LEN, LLADDR(&sdl));
}

static
DECL_CMD_FUNC(set80211addmac, val, d)
{
	set80211macmac(s, IEEE80211_IOC_ADDMAC, val);
}

static
DECL_CMD_FUNC(set80211delmac, val, d)
{
	set80211macmac(s, IEEE80211_IOC_DELMAC, val);
}

static
DECL_CMD_FUNC(set80211kickmac, val, d)
{
	char *temp;
	struct sockaddr_dl sdl;
	struct ieee80211req_mlme mlme;

	temp = malloc(strlen(val) + 2); /* ':' and '\0' */
	if (temp == NULL)
		errx(1, "malloc failed");
	temp[0] = ':';
	strcpy(temp + 1, val);
	sdl.sdl_len = sizeof(sdl);
	link_addr(temp, &sdl);
	free(temp);
	if (sdl.sdl_alen != IEEE80211_ADDR_LEN)
		errx(1, "malformed link-level address");
	memset(&mlme, 0, sizeof(mlme));
	mlme.im_op = IEEE80211_MLME_DEAUTH;
	mlme.im_reason = IEEE80211_REASON_AUTH_EXPIRE;
	memcpy(mlme.im_macaddr, LLADDR(&sdl), IEEE80211_ADDR_LEN);
	set80211(s, IEEE80211_IOC_MLME, 0, sizeof(mlme), &mlme);
}

static
DECL_CMD_FUNC(set80211maccmd, val, d)
{
	set80211(s, IEEE80211_IOC_MACCMD, d, 0, NULL);
}

static void
set80211pureg(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_PUREG, d, 0, NULL);
}

static void
set80211bgscan(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_BGSCAN, d, 0, NULL);
}

static
DECL_CMD_FUNC(set80211bgscanidle, val, d)
{
	set80211(s, IEEE80211_IOC_BGSCAN_IDLE, atoi(val), 0, NULL);
}

static
DECL_CMD_FUNC(set80211bgscanintvl, val, d)
{
	set80211(s, IEEE80211_IOC_BGSCAN_INTERVAL, atoi(val), 0, NULL);
}

static
DECL_CMD_FUNC(set80211scanvalid, val, d)
{
	set80211(s, IEEE80211_IOC_SCANVALID, atoi(val), 0, NULL);
}

/*
 * Parse an optional trailing specification of which netbands
 * to apply a parameter to.  This is basically the same syntax
 * as used for channels but you can concatenate to specify
 * multiple.  For example:
 *	14:abg		apply to 11a, 11b, and 11g
 *	6:ht		apply to 11na and 11ng
 * We don't make a big effort to catch silly things; this is
 * really a convenience mechanism.
 */
static int
getmodeflags(const char *val)
{
	const char *cp;
	int flags;

	flags = 0;

	cp = strchr(val, ':');
	if (cp != NULL) {
		for (cp++; isalpha((int) *cp); cp++) {
			/* accept mixed case */
			int c = *cp;
			if (isupper(c))
				c = tolower(c);
			switch (c) {
			case 'a':		/* 802.11a */
				flags |= IEEE80211_CHAN_A;
				break;
			case 'b':		/* 802.11b */
				flags |= IEEE80211_CHAN_B;
				break;
			case 'g':		/* 802.11g */
				flags |= IEEE80211_CHAN_G;
				break;
			case 'h':		/* ht = 802.11n */
			case 'n':		/* 802.11n */
				flags |= IEEE80211_CHAN_HT;
				break;
			case 'd':		/* dt = Atheros Dynamic Turbo */
				flags |= IEEE80211_CHAN_TURBO;
				break;
			case 't':		/* ht, dt, st, t */
				/* dt and unadorned t specify Dynamic Turbo */
				if ((flags & (IEEE80211_CHAN_STURBO|IEEE80211_CHAN_HT)) == 0)
					flags |= IEEE80211_CHAN_TURBO;
				break;
			case 's':		/* st = Atheros Static Turbo */
				flags |= IEEE80211_CHAN_STURBO;
				break;
			default:
				errx(-1, "%s: Invalid mode attribute %c\n",
				    val, *cp);
			}
		}
	}
	return flags;
}

#define	IEEE80211_CHAN_HTA	(IEEE80211_CHAN_HT|IEEE80211_CHAN_5GHZ)
#define	IEEE80211_CHAN_HTG	(IEEE80211_CHAN_HT|IEEE80211_CHAN_2GHZ)

#define	_APPLY(_flags, _base, _param, _v) do {				\
    if (_flags & IEEE80211_CHAN_HT) {					\
	    if ((_flags & (IEEE80211_CHAN_5GHZ|IEEE80211_CHAN_2GHZ)) == 0) {\
		    _base.params[IEEE80211_MODE_11NA]._param = _v;	\
		    _base.params[IEEE80211_MODE_11NG]._param = _v;	\
	    } else if (_flags & IEEE80211_CHAN_5GHZ)			\
		    _base.params[IEEE80211_MODE_11NA]._param = _v;	\
	    else							\
		    _base.params[IEEE80211_MODE_11NG]._param = _v;	\
    }									\
    if (_flags & IEEE80211_CHAN_TURBO) {				\
	    if ((_flags & (IEEE80211_CHAN_5GHZ|IEEE80211_CHAN_2GHZ)) == 0) {\
		    _base.params[IEEE80211_MODE_TURBO_A]._param = _v;	\
		    _base.params[IEEE80211_MODE_TURBO_G]._param = _v;	\
	    } else if (_flags & IEEE80211_CHAN_5GHZ)			\
		    _base.params[IEEE80211_MODE_TURBO_A]._param = _v;	\
	    else							\
		    _base.params[IEEE80211_MODE_TURBO_G]._param = _v;	\
    }									\
    if (_flags & IEEE80211_CHAN_STURBO)					\
	    _base.params[IEEE80211_MODE_STURBO_A]._param = _v;		\
    if ((_flags & IEEE80211_CHAN_A) == IEEE80211_CHAN_A)		\
	    _base.params[IEEE80211_MODE_11A]._param = _v;		\
    if ((_flags & IEEE80211_CHAN_G) == IEEE80211_CHAN_G)		\
	    _base.params[IEEE80211_MODE_11G]._param = _v;		\
    if ((_flags & IEEE80211_CHAN_B) == IEEE80211_CHAN_B)		\
	    _base.params[IEEE80211_MODE_11B]._param = _v;		\
} while (0)
#define	_APPLY1(_flags, _base, _param, _v) do {				\
    if (_flags & IEEE80211_CHAN_HT) {					\
	    if (_flags & IEEE80211_CHAN_5GHZ)				\
		    _base.params[IEEE80211_MODE_11NA]._param = _v;	\
	    else							\
		    _base.params[IEEE80211_MODE_11NG]._param = _v;	\
    } else if ((_flags & IEEE80211_CHAN_108A) == IEEE80211_CHAN_108A)	\
	    _base.params[IEEE80211_MODE_TURBO_A]._param = _v;		\
    else if ((_flags & IEEE80211_CHAN_108G) == IEEE80211_CHAN_108G)	\
	    _base.params[IEEE80211_MODE_TURBO_G]._param = _v;		\
    else if ((_flags & IEEE80211_CHAN_ST) == IEEE80211_CHAN_ST)		\
	    _base.params[IEEE80211_MODE_STURBO_A]._param = _v;		\
    else if ((_flags & IEEE80211_CHAN_A) == IEEE80211_CHAN_A)		\
	    _base.params[IEEE80211_MODE_11A]._param = _v;		\
    else if ((_flags & IEEE80211_CHAN_G) == IEEE80211_CHAN_G)		\
	    _base.params[IEEE80211_MODE_11G]._param = _v;		\
    else if ((_flags & IEEE80211_CHAN_B) == IEEE80211_CHAN_B)		\
	    _base.params[IEEE80211_MODE_11B]._param = _v;		\
} while (0)
#define	_APPLY_RATE(_flags, _base, _param, _v) do {			\
    if (_flags & IEEE80211_CHAN_HT) {					\
	    if ((_flags & (IEEE80211_CHAN_5GHZ|IEEE80211_CHAN_2GHZ)) == 0) {\
		    _base.params[IEEE80211_MODE_11NA]._param = _v|0x80;	\
		    _base.params[IEEE80211_MODE_11NG]._param = _v|0x80;	\
	    } else if (_flags & IEEE80211_CHAN_5GHZ)			\
		    _base.params[IEEE80211_MODE_11NA]._param = _v|0x80;	\
	    else							\
		    _base.params[IEEE80211_MODE_11NG]._param = _v|0x80;	\
    }									\
    if (_flags & IEEE80211_CHAN_TURBO) {				\
	    if ((_flags & (IEEE80211_CHAN_5GHZ|IEEE80211_CHAN_2GHZ)) == 0) {\
		    _base.params[IEEE80211_MODE_TURBO_A]._param = 2*_v;	\
		    _base.params[IEEE80211_MODE_TURBO_G]._param = 2*_v;	\
	    } else if (_flags & IEEE80211_CHAN_5GHZ)			\
		    _base.params[IEEE80211_MODE_TURBO_A]._param = 2*_v;	\
	    else							\
		    _base.params[IEEE80211_MODE_TURBO_G]._param = 2*_v;	\
    }									\
    if (_flags & IEEE80211_CHAN_STURBO)					\
	    _base.params[IEEE80211_MODE_STURBO_A]._param = 2*_v;	\
    if ((_flags & IEEE80211_CHAN_A) == IEEE80211_CHAN_A)		\
	    _base.params[IEEE80211_MODE_11A]._param = 2*_v;		\
    if ((_flags & IEEE80211_CHAN_G) == IEEE80211_CHAN_G)		\
	    _base.params[IEEE80211_MODE_11G]._param = (_v == 5 ? 11 : 2*_v);\
    if ((_flags & IEEE80211_CHAN_B) == IEEE80211_CHAN_B)		\
	    _base.params[IEEE80211_MODE_11B]._param = (_v == 5 ? 11 : 2*_v);\
} while (0)
#define	_APPLY_RATE1(_flags, _base, _param, _v) do {			\
    if (_flags & IEEE80211_CHAN_HT) {					\
	    if (_flags & IEEE80211_CHAN_5GHZ)				\
		    _base.params[IEEE80211_MODE_11NA]._param = _v|0x80;	\
	    else							\
		    _base.params[IEEE80211_MODE_11NG]._param = _v|0x80;	\
    } else if ((_flags & IEEE80211_CHAN_108A) == IEEE80211_CHAN_108A)	\
	    _base.params[IEEE80211_MODE_TURBO_A]._param = 2*_v;		\
    else if ((_flags & IEEE80211_CHAN_108G) == IEEE80211_CHAN_108G)	\
	    _base.params[IEEE80211_MODE_TURBO_G]._param = 2*_v;		\
    else if ((_flags & IEEE80211_CHAN_ST) == IEEE80211_CHAN_ST)		\
	    _base.params[IEEE80211_MODE_STURBO_A]._param = 2*_v;	\
    else if ((_flags & IEEE80211_CHAN_A) == IEEE80211_CHAN_A)		\
	    _base.params[IEEE80211_MODE_11A]._param = 2*_v;		\
    else if ((_flags & IEEE80211_CHAN_G) == IEEE80211_CHAN_G)		\
	    _base.params[IEEE80211_MODE_11G]._param = (_v == 5 ? 11 : 2*_v);\
    else if ((_flags & IEEE80211_CHAN_B) == IEEE80211_CHAN_B)		\
	    _base.params[IEEE80211_MODE_11B]._param = (_v == 5 ? 11 : 2*_v);\
} while (0)

static
DECL_CMD_FUNC(set80211roamrssi, val, d)
{
	double v = atof(val);
	int rssi, flags;

	rssi = (int) (2*v);
	if (rssi != 2*v)
		errx(-1, "invalid rssi (must be .5 dBm units)");
	flags = getmodeflags(val);
	getroam(s);
	if (flags == 0) {		/* NB: no flags => current channel */
		flags = getcurchan(s)->ic_flags;
		_APPLY1(flags, roamparams, rssi, rssi);
	} else
		_APPLY(flags, roamparams, rssi, rssi);
	callback_register(setroam_cb, &roamparams);
}

static
DECL_CMD_FUNC(set80211roamrate, val, d)
{
	int v = atoi(val), flags;

	flags = getmodeflags(val);
	getroam(s);
	if (flags == 0) {		/* NB: no flags => current channel */
		flags = getcurchan(s)->ic_flags;
		_APPLY_RATE1(flags, roamparams, rate, v);
	} else
		_APPLY_RATE(flags, roamparams, rate, v);
	callback_register(setroam_cb, &roamparams);
}

static
DECL_CMD_FUNC(set80211mcastrate, val, d)
{
	int v = atoi(val), flags;

	flags = getmodeflags(val);
	gettxparams(s);
	if (flags == 0) {		/* NB: no flags => current channel */
		flags = getcurchan(s)->ic_flags;
		_APPLY_RATE1(flags, txparams, mcastrate, v);
	} else
		_APPLY_RATE(flags, txparams, mcastrate, v);
	callback_register(settxparams_cb, &txparams);
}

static
DECL_CMD_FUNC(set80211mgtrate, val, d)
{
	int v = atoi(val), flags;

	flags = getmodeflags(val);
	gettxparams(s);
	if (flags == 0) {		/* NB: no flags => current channel */
		flags = getcurchan(s)->ic_flags;
		_APPLY_RATE1(flags, txparams, mgmtrate, v);
	} else
		_APPLY_RATE(flags, txparams, mgmtrate, v);
	callback_register(settxparams_cb, &txparams);
}

static
DECL_CMD_FUNC(set80211ucastrate, val, d)
{
	int v, flags;

	gettxparams(s);
	flags = getmodeflags(val);
	if (isanyarg(val)) {
		if (flags == 0) {	/* NB: no flags => current channel */
			flags = getcurchan(s)->ic_flags;
			_APPLY1(flags, txparams, ucastrate,
			    IEEE80211_FIXED_RATE_NONE);
		} else
			_APPLY(flags, txparams, ucastrate,
			    IEEE80211_FIXED_RATE_NONE);
	} else {
		v = atoi(val);
		if (flags == 0) {	/* NB: no flags => current channel */
			flags = getcurchan(s)->ic_flags;
			_APPLY_RATE1(flags, txparams, ucastrate, v);
		} else
			_APPLY_RATE(flags, txparams, ucastrate, v);
	}
	callback_register(settxparams_cb, &txparams);
}

static
DECL_CMD_FUNC(set80211maxretry, val, d)
{
	int v = atoi(val), flags;

	flags = getmodeflags(val);
	gettxparams(s);
	if (flags == 0) {		/* NB: no flags => current channel */
		flags = getcurchan(s)->ic_flags;
		_APPLY1(flags, txparams, maxretry, v);
	} else
		_APPLY(flags, txparams, maxretry, v);
	callback_register(settxparams_cb, &txparams);
}
#undef _APPLY_RATE
#undef _APPLY
#undef IEEE80211_CHAN_HTA
#undef IEEE80211_CHAN_HTG

static
DECL_CMD_FUNC(set80211fragthreshold, val, d)
{
	set80211(s, IEEE80211_IOC_FRAGTHRESHOLD,
		isundefarg(val) ? IEEE80211_FRAG_MAX : atoi(val), 0, NULL);
}

static
DECL_CMD_FUNC(set80211bmissthreshold, val, d)
{
	set80211(s, IEEE80211_IOC_BMISSTHRESHOLD,
		isundefarg(val) ? IEEE80211_HWBMISS_MAX : atoi(val), 0, NULL);
}

static void
set80211burst(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_BURST, d, 0, NULL);
}

static void
set80211doth(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_DOTH, d, 0, NULL);
}

static void
set80211dfs(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_DFS, d, 0, NULL);
}

static void
set80211shortgi(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_SHORTGI,
		d ? (IEEE80211_HTCAP_SHORTGI20 | IEEE80211_HTCAP_SHORTGI40) : 0,
		0, NULL);
}

static void
set80211ampdu(const char *val, int d, int s, const struct afswtch *rafp)
{
	int ampdu;

	if (get80211val(s, IEEE80211_IOC_AMPDU, &ampdu) < 0)
		errx(-1, "cannot get AMPDU setting");
	if (d < 0) {
		d = -d;
		ampdu &= ~d;
	} else
		ampdu |= d;
	set80211(s, IEEE80211_IOC_AMPDU, ampdu, 0, NULL);
}

static
DECL_CMD_FUNC(set80211ampdulimit, val, d)
{
	int v;

	switch (atoi(val)) {
	case 8:
	case 8*1024:
		v = IEEE80211_HTCAP_MAXRXAMPDU_8K;
		break;
	case 16:
	case 16*1024:
		v = IEEE80211_HTCAP_MAXRXAMPDU_16K;
		break;
	case 32:
	case 32*1024:
		v = IEEE80211_HTCAP_MAXRXAMPDU_32K;
		break;
	case 64:
	case 64*1024:
		v = IEEE80211_HTCAP_MAXRXAMPDU_64K;
		break;
	default:
		errx(-1, "invalid A-MPDU limit %s", val);
	}
	set80211(s, IEEE80211_IOC_AMPDU_LIMIT, v, 0, NULL);
}

static
DECL_CMD_FUNC(set80211ampdudensity, val, d)
{
	int v;

	if (isanyarg(val) || strcasecmp(val, "na") == 0)
		v = IEEE80211_HTCAP_MPDUDENSITY_NA;
	else switch ((int)(atof(val)*4)) {
	case 0:
		v = IEEE80211_HTCAP_MPDUDENSITY_NA;
		break;
	case 1:
		v = IEEE80211_HTCAP_MPDUDENSITY_025;
		break;
	case 2:
		v = IEEE80211_HTCAP_MPDUDENSITY_05;
		break;
	case 4:
		v = IEEE80211_HTCAP_MPDUDENSITY_1;
		break;
	case 8:
		v = IEEE80211_HTCAP_MPDUDENSITY_2;
		break;
	case 16:
		v = IEEE80211_HTCAP_MPDUDENSITY_4;
		break;
	case 32:
		v = IEEE80211_HTCAP_MPDUDENSITY_8;
		break;
	case 64:
		v = IEEE80211_HTCAP_MPDUDENSITY_16;
		break;
	default:
		errx(-1, "invalid A-MPDU density %s", val);
	}
	set80211(s, IEEE80211_IOC_AMPDU_DENSITY, v, 0, NULL);
}

static void
set80211amsdu(const char *val, int d, int s, const struct afswtch *rafp)
{
	int amsdu;

	if (get80211val(s, IEEE80211_IOC_AMSDU, &amsdu) < 0)
		err(-1, "cannot get AMSDU setting");
	if (d < 0) {
		d = -d;
		amsdu &= ~d;
	} else
		amsdu |= d;
	set80211(s, IEEE80211_IOC_AMSDU, amsdu, 0, NULL);
}

static
DECL_CMD_FUNC(set80211amsdulimit, val, d)
{
	set80211(s, IEEE80211_IOC_AMSDU_LIMIT, atoi(val), 0, NULL);
}

static void
set80211puren(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_PUREN, d, 0, NULL);
}

static void
set80211htcompat(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_HTCOMPAT, d, 0, NULL);
}

static void
set80211htconf(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_HTCONF, d, 0, NULL);
	htconf = d;
}

static void
set80211dwds(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_DWDS, d, 0, NULL);
}

static void
set80211inact(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_INACTIVITY, d, 0, NULL);
}

static void
set80211tsn(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_TSN, d, 0, NULL);
}

static void
set80211dotd(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_DOTD, d, 0, NULL);
}

static void
set80211smps(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_SMPS, d, 0, NULL);
}

static void
set80211rifs(const char *val, int d, int s, const struct afswtch *rafp)
{
	set80211(s, IEEE80211_IOC_RIFS, d, 0, NULL);
}

static
DECL_CMD_FUNC(set80211tdmaslot, val, d)
{
	set80211(s, IEEE80211_IOC_TDMA_SLOT, atoi(val), 0, NULL);
}

static
DECL_CMD_FUNC(set80211tdmaslotcnt, val, d)
{
	set80211(s, IEEE80211_IOC_TDMA_SLOTCNT, atoi(val), 0, NULL);
}

static
DECL_CMD_FUNC(set80211tdmaslotlen, val, d)
{
	set80211(s, IEEE80211_IOC_TDMA_SLOTLEN, atoi(val), 0, NULL);
}

static
DECL_CMD_FUNC(set80211tdmabintval, val, d)
{
	set80211(s, IEEE80211_IOC_TDMA_BINTERVAL, atoi(val), 0, NULL);
}

static int
regdomain_sort(const void *a, const void *b)
{
#define	CHAN_ALL \
	(IEEE80211_CHAN_ALLTURBO|IEEE80211_CHAN_HALF|IEEE80211_CHAN_QUARTER)
	const struct ieee80211_channel *ca = a;
	const struct ieee80211_channel *cb = b;

	return ca->ic_freq == cb->ic_freq ?
	    (ca->ic_flags & CHAN_ALL) - (cb->ic_flags & CHAN_ALL) :
	    ca->ic_freq - cb->ic_freq;
#undef CHAN_ALL
}

static const struct ieee80211_channel *
chanlookup(const struct ieee80211_channel chans[], int nchans,
	int freq, int flags)
{
	int i;

	flags &= IEEE80211_CHAN_ALLTURBO;
	for (i = 0; i < nchans; i++) {
		const struct ieee80211_channel *c = &chans[i];
		if (c->ic_freq == freq &&
		    (c->ic_flags & IEEE80211_CHAN_ALLTURBO) == flags)
			return c;
	}
	return NULL;
}

static int
chanfind(const struct ieee80211_channel chans[], int nchans, int flags)
{
	int i;

	for (i = 0; i < nchans; i++) {
		const struct ieee80211_channel *c = &chans[i];
		if ((c->ic_flags & flags) == flags)
			return 1;
	}
	return 0;
}

static void
regdomain_addchans(struct ieee80211req_chaninfo *ci,
	const netband_head *bands,
	const struct ieee80211_regdomain *reg,
	uint32_t chanFlags,
	const struct ieee80211req_chaninfo *avail)
{
	const struct netband *nb;
	const struct freqband *b;
	struct ieee80211_channel *c, *prev;
	int freq, channelSep, hasHalfChans, hasQuarterChans;

	channelSep = (chanFlags & IEEE80211_CHAN_2GHZ) ? 0 : 40;
	hasHalfChans = chanfind(avail->ic_chans, avail->ic_nchans,
	    IEEE80211_CHAN_HALF |
	       (chanFlags & (IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_5GHZ)));
	hasQuarterChans = chanfind(avail->ic_chans, avail->ic_nchans,
	    IEEE80211_CHAN_QUARTER |
	        (chanFlags & (IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_5GHZ)));
	LIST_FOREACH(nb, bands, next) {
		b = nb->band;
		if (verbose) {
			printf("%s:", __func__);
			printb(" chanFlags", chanFlags, IEEE80211_CHAN_BITS);
			printb(" bandFlags", nb->flags | b->flags,
			    IEEE80211_CHAN_BITS);
			putchar('\n');
		}
		prev = NULL;
		for (freq = b->freqStart; freq <= b->freqEnd; freq += b->chanSep) {
			uint32_t flags = nb->flags | b->flags;

			/* check if device can operate on this frequency */
			/*
			 * XXX GSM frequency mapping is handled in the kernel
			 * so we cannot find them in the calibration table;
			 * just construct the list and the kernel will reject
			 * if it's wrong.
			 */
			if (chanlookup(avail->ic_chans, avail->ic_nchans, freq, chanFlags) == NULL &&
			    (flags & IEEE80211_CHAN_GSM) == 0) {
				if (verbose) {
					printf("%u: skip, ", freq);
					printb("flags", chanFlags,
					    IEEE80211_CHAN_BITS);
					printf(" not available\n");
				}
				continue;
			}
			if ((flags & IEEE80211_CHAN_HALF) && !hasHalfChans) {
				if (verbose)
					printf("%u: skip, device does not "
					    "support half-rate channel\n",
					    freq);
				continue;
			}
			if ((flags & IEEE80211_CHAN_QUARTER) &&
			    !hasQuarterChans) {
				if (verbose)
					printf("%u: skip, device does not "
					    "support quarter-rate channel\n",
					    freq);
				continue;
			}
			if ((flags & IEEE80211_CHAN_HT20) &&
			    (chanFlags & IEEE80211_CHAN_HT20) == 0) {
				if (verbose)
					printf("%u: skip, device does not "
					    "support HT20 operation\n", freq);
				continue;
			}
			if ((flags & IEEE80211_CHAN_HT40) &&
			    (chanFlags & IEEE80211_CHAN_HT40) == 0) {
				if (verbose)
					printf("%u: skip, device does not "
					    "support HT40 operation\n", freq);
				continue;
			}
			if ((flags & REQ_ECM) && !reg->ecm) {
				if (verbose)
					printf("%u: skip, ECM channel\n", freq);
				continue;
			}
			if ((flags & REQ_OUTDOOR) && reg->location == 'I') {
				if (verbose)
					printf("%u: skip, outdoor channel\n", freq);
				continue;
			}
			if ((flags & IEEE80211_CHAN_HT40) &&
			    prev != NULL && (freq - prev->ic_freq) < channelSep) {
				if (verbose)
					printf("%u: skip, only %u channel "
					    "separation, need %d\n", freq, 
					    freq - prev->ic_freq, channelSep);
				continue;
			}
			if (ci->ic_nchans == IEEE80211_CHAN_MAX) {
				if (verbose)
					printf("%u: skip, channel table full\n",
					    freq);
				break;
			}
			c = &ci->ic_chans[ci->ic_nchans++];
			memset(c, 0, sizeof(*c));
			c->ic_freq = freq;
			c->ic_flags = chanFlags |
			    (flags &~ (REQ_FLAGS | IEEE80211_CHAN_HT40));
			if (c->ic_flags & IEEE80211_CHAN_DFS)
				c->ic_maxregpower = nb->maxPowerDFS;
			else
				c->ic_maxregpower = nb->maxPower;
			if (verbose) {
				printf("[%3d] add freq %u ",
				    ci->ic_nchans-1, c->ic_freq);
				printb("flags", c->ic_flags, IEEE80211_CHAN_BITS);
				printf(" power %u\n", c->ic_maxregpower);
			}
			/* NB: kernel fills in other fields */
			prev = c;
		}
	}
}

static void
regdomain_makechannels(
	struct ieee80211_regdomain_req *req,
	const struct ieee80211_devcaps_req *dc)
{
	struct regdata *rdp = getregdata();
	const struct country *cc;
	const struct ieee80211_regdomain *reg = &req->rd;
	struct ieee80211req_chaninfo *ci = &req->chaninfo;
	const struct regdomain *rd;

	/*
	 * Locate construction table for new channel list.  We treat
	 * the regdomain/SKU as definitive so a country can be in
	 * multiple with different properties (e.g. US in FCC+FCC3).
	 * If no regdomain is specified then we fallback on the country
	 * code to find the associated regdomain since countries always
	 * belong to at least one regdomain.
	 */
	if (reg->regdomain == 0) {
		cc = lib80211_country_findbycc(rdp, reg->country);
		if (cc == NULL)
			errx(1, "internal error, country %d not found",
			    reg->country);
		rd = cc->rd;
	} else
		rd = lib80211_regdomain_findbysku(rdp, reg->regdomain);
	if (rd == NULL)
		errx(1, "internal error, regdomain %d not found",
			    reg->regdomain);
	if (rd->sku != SKU_DEBUG) {
		/*
		 * regdomain_addchans incrememnts the channel count for
		 * each channel it adds so initialize ic_nchans to zero.
		 * Note that we know we have enough space to hold all possible
		 * channels because the devcaps list size was used to
		 * allocate our request.
		 */
		ci->ic_nchans = 0;
		if (!LIST_EMPTY(&rd->bands_11b))
			regdomain_addchans(ci, &rd->bands_11b, reg,
			    IEEE80211_CHAN_B, &dc->dc_chaninfo);
		if (!LIST_EMPTY(&rd->bands_11g))
			regdomain_addchans(ci, &rd->bands_11g, reg,
			    IEEE80211_CHAN_G, &dc->dc_chaninfo);
		if (!LIST_EMPTY(&rd->bands_11a))
			regdomain_addchans(ci, &rd->bands_11a, reg,
			    IEEE80211_CHAN_A, &dc->dc_chaninfo);
		if (!LIST_EMPTY(&rd->bands_11na)) {
			regdomain_addchans(ci, &rd->bands_11na, reg,
			    IEEE80211_CHAN_A | IEEE80211_CHAN_HT20,
			    &dc->dc_chaninfo);
			regdomain_addchans(ci, &rd->bands_11na, reg,
			    IEEE80211_CHAN_A | IEEE80211_CHAN_HT40U,
			    &dc->dc_chaninfo);
			regdomain_addchans(ci, &rd->bands_11na, reg,
			    IEEE80211_CHAN_A | IEEE80211_CHAN_HT40D,
			    &dc->dc_chaninfo);
		}
		if (!LIST_EMPTY(&rd->bands_11ng)) {
			regdomain_addchans(ci, &rd->bands_11ng, reg,
			    IEEE80211_CHAN_G | IEEE80211_CHAN_HT20,
			    &dc->dc_chaninfo);
			regdomain_addchans(ci, &rd->bands_11ng, reg,
			    IEEE80211_CHAN_G | IEEE80211_CHAN_HT40U,
			    &dc->dc_chaninfo);
			regdomain_addchans(ci, &rd->bands_11ng, reg,
			    IEEE80211_CHAN_G | IEEE80211_CHAN_HT40D,
			    &dc->dc_chaninfo);
		}
		qsort(ci->ic_chans, ci->ic_nchans, sizeof(ci->ic_chans[0]),
		    regdomain_sort);
	} else
		memcpy(ci, &dc->dc_chaninfo,
		    IEEE80211_CHANINFO_SPACE(&dc->dc_chaninfo));
}

static void
list_countries(void)
{
	struct regdata *rdp = getregdata();
	const struct country *cp;
	const struct regdomain *dp;
	int i;

	i = 0;
	printf("\nCountry codes:\n");
	LIST_FOREACH(cp, &rdp->countries, next) {
		printf("%2s %-15.15s%s", cp->isoname,
		    cp->name, ((i+1)%4) == 0 ? "\n" : " ");
		i++;
	}
	i = 0;
	printf("\nRegulatory domains:\n");
	LIST_FOREACH(dp, &rdp->domains, next) {
		printf("%-15.15s%s", dp->name, ((i+1)%4) == 0 ? "\n" : " ");
		i++;
	}
	printf("\n");
}

static void
defaultcountry(const struct regdomain *rd)
{
	struct regdata *rdp = getregdata();
	const struct country *cc;

	cc = lib80211_country_findbycc(rdp, rd->cc->code);
	if (cc == NULL)
		errx(1, "internal error, ISO country code %d not "
		    "defined for regdomain %s", rd->cc->code, rd->name);
	regdomain.country = cc->code;
	regdomain.isocc[0] = cc->isoname[0];
	regdomain.isocc[1] = cc->isoname[1];
}

static
DECL_CMD_FUNC(set80211regdomain, val, d)
{
	struct regdata *rdp = getregdata();
	const struct regdomain *rd;

	rd = lib80211_regdomain_findbyname(rdp, val);
	if (rd == NULL) {
		char *eptr;
		long sku = strtol(val, &eptr, 0);

		if (eptr != val)
			rd = lib80211_regdomain_findbysku(rdp, sku);
		if (eptr == val || rd == NULL)
			errx(1, "unknown regdomain %s", val);
	}
	getregdomain(s);
	regdomain.regdomain = rd->sku;
	if (regdomain.country == 0 && rd->cc != NULL) {
		/*
		 * No country code setup and there's a default
		 * one for this regdomain fill it in.
		 */
		defaultcountry(rd);
	}
	callback_register(setregdomain_cb, &regdomain);
}

static
DECL_CMD_FUNC(set80211country, val, d)
{
	struct regdata *rdp = getregdata();
	const struct country *cc;

	cc = lib80211_country_findbyname(rdp, val);
	if (cc == NULL) {
		char *eptr;
		long code = strtol(val, &eptr, 0);

		if (eptr != val)
			cc = lib80211_country_findbycc(rdp, code);
		if (eptr == val || cc == NULL)
			errx(1, "unknown ISO country code %s", val);
	}
	getregdomain(s);
	regdomain.regdomain = cc->rd->sku;
	regdomain.country = cc->code;
	regdomain.isocc[0] = cc->isoname[0];
	regdomain.isocc[1] = cc->isoname[1];
	callback_register(setregdomain_cb, &regdomain);
}

static void
set80211location(const char *val, int d, int s, const struct afswtch *rafp)
{
	getregdomain(s);
	regdomain.location = d;
	callback_register(setregdomain_cb, &regdomain);
}

static void
set80211ecm(const char *val, int d, int s, const struct afswtch *rafp)
{
	getregdomain(s);
	regdomain.ecm = d;
	callback_register(setregdomain_cb, &regdomain);
}

static void
LINE_INIT(char c)
{
	spacer = c;
	if (c == '\t')
		col = 8;
	else
		col = 1;
}

static void
LINE_BREAK(void)
{
	if (spacer != '\t') {
		printf("\n");
		spacer = '\t';
	}
	col = 8;		/* 8-col tab */
}

static void
LINE_CHECK(const char *fmt, ...)
{
	char buf[80];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf+1, sizeof(buf)-1, fmt, ap);
	va_end(ap);
	col += 1+n;
	if (col > MAXCOL) {
		LINE_BREAK();
		col += n;
	}
	buf[0] = spacer;
	printf("%s", buf);
	spacer = ' ';
}

static int
getmaxrate(const uint8_t rates[15], uint8_t nrates)
{
	int i, maxrate = -1;

	for (i = 0; i < nrates; i++) {
		int rate = rates[i] & IEEE80211_RATE_VAL;
		if (rate > maxrate)
			maxrate = rate;
	}
	return maxrate / 2;
}

static const char *
getcaps(int capinfo)
{
	static char capstring[32];
	char *cp = capstring;

	if (capinfo & IEEE80211_CAPINFO_ESS)
		*cp++ = 'E';
	if (capinfo & IEEE80211_CAPINFO_IBSS)
		*cp++ = 'I';
	if (capinfo & IEEE80211_CAPINFO_CF_POLLABLE)
		*cp++ = 'c';
	if (capinfo & IEEE80211_CAPINFO_CF_POLLREQ)
		*cp++ = 'C';
	if (capinfo & IEEE80211_CAPINFO_PRIVACY)
		*cp++ = 'P';
	if (capinfo & IEEE80211_CAPINFO_SHORT_PREAMBLE)
		*cp++ = 'S';
	if (capinfo & IEEE80211_CAPINFO_PBCC)
		*cp++ = 'B';
	if (capinfo & IEEE80211_CAPINFO_CHNL_AGILITY)
		*cp++ = 'A';
	if (capinfo & IEEE80211_CAPINFO_SHORT_SLOTTIME)
		*cp++ = 's';
	if (capinfo & IEEE80211_CAPINFO_RSN)
		*cp++ = 'R';
	if (capinfo & IEEE80211_CAPINFO_DSSSOFDM)
		*cp++ = 'D';
	*cp = '\0';
	return capstring;
}

static const char *
getflags(int flags)
{
	static char flagstring[32];
	char *cp = flagstring;

	if (flags & IEEE80211_NODE_AUTH)
		*cp++ = 'A';
	if (flags & IEEE80211_NODE_QOS)
		*cp++ = 'Q';
	if (flags & IEEE80211_NODE_ERP)
		*cp++ = 'E';
	if (flags & IEEE80211_NODE_PWR_MGT)
		*cp++ = 'P';
	if (flags & IEEE80211_NODE_HT) {
		*cp++ = 'H';
		if (flags & IEEE80211_NODE_HTCOMPAT)
			*cp++ = '+';
	}
	if (flags & IEEE80211_NODE_WPS)
		*cp++ = 'W';
	if (flags & IEEE80211_NODE_TSN)
		*cp++ = 'N';
	if (flags & IEEE80211_NODE_AMPDU_TX)
		*cp++ = 'T';
	if (flags & IEEE80211_NODE_AMPDU_RX)
		*cp++ = 'R';
	if (flags & IEEE80211_NODE_MIMO_PS) {
		*cp++ = 'M';
		if (flags & IEEE80211_NODE_MIMO_RTS)
			*cp++ = '+';
	}
	if (flags & IEEE80211_NODE_RIFS)
		*cp++ = 'I';
	*cp = '\0';
	return flagstring;
}

static void
printie(const char* tag, const uint8_t *ie, size_t ielen, int maxlen)
{
	printf("%s", tag);
	if (verbose) {
		maxlen -= strlen(tag)+2;
		if (2*ielen > maxlen)
			maxlen--;
		printf("<");
		for (; ielen > 0; ie++, ielen--) {
			if (maxlen-- <= 0)
				break;
			printf("%02x", *ie);
		}
		if (ielen != 0)
			printf("-");
		printf(">");
	}
}

#define LE_READ_2(p)					\
	((u_int16_t)					\
	 ((((const u_int8_t *)(p))[0]      ) |		\
	  (((const u_int8_t *)(p))[1] <<  8)))
#define LE_READ_4(p)					\
	((u_int32_t)					\
	 ((((const u_int8_t *)(p))[0]      ) |		\
	  (((const u_int8_t *)(p))[1] <<  8) |		\
	  (((const u_int8_t *)(p))[2] << 16) |		\
	  (((const u_int8_t *)(p))[3] << 24)))

/*
 * NB: The decoding routines assume a properly formatted ie
 *     which should be safe as the kernel only retains them
 *     if they parse ok.
 */

static void
printwmeparam(const char *tag, const u_int8_t *ie, size_t ielen, int maxlen)
{
#define	MS(_v, _f)	(((_v) & _f) >> _f##_S)
	static const char *acnames[] = { "BE", "BK", "VO", "VI" };
	const struct ieee80211_wme_param *wme =
	    (const struct ieee80211_wme_param *) ie;
	int i;

	printf("%s", tag);
	if (!verbose)
		return;
	printf("<qosinfo 0x%x", wme->param_qosInfo);
	ie += offsetof(struct ieee80211_wme_param, params_acParams);
	for (i = 0; i < WME_NUM_AC; i++) {
		const struct ieee80211_wme_acparams *ac =
		    &wme->params_acParams[i];

		printf(" %s[%saifsn %u cwmin %u cwmax %u txop %u]"
			, acnames[i]
			, MS(ac->acp_aci_aifsn, WME_PARAM_ACM) ? "acm " : ""
			, MS(ac->acp_aci_aifsn, WME_PARAM_AIFSN)
			, MS(ac->acp_logcwminmax, WME_PARAM_LOGCWMIN)
			, MS(ac->acp_logcwminmax, WME_PARAM_LOGCWMAX)
			, LE_READ_2(&ac->acp_txop)
		);
	}
	printf(">");
#undef MS
}

static void
printwmeinfo(const char *tag, const u_int8_t *ie, size_t ielen, int maxlen)
{
	printf("%s", tag);
	if (verbose) {
		const struct ieee80211_wme_info *wme =
		    (const struct ieee80211_wme_info *) ie;
		printf("<version 0x%x info 0x%x>",
		    wme->wme_version, wme->wme_info);
	}
}

static void
printhtcap(const char *tag, const u_int8_t *ie, size_t ielen, int maxlen)
{
	printf("%s", tag);
	if (verbose) {
		const struct ieee80211_ie_htcap *htcap =
		    (const struct ieee80211_ie_htcap *) ie;
		const char *sep;
		int i, j;

		printf("<cap 0x%x param 0x%x",
		    LE_READ_2(&htcap->hc_cap), htcap->hc_param);
		printf(" mcsset[");
		sep = "";
		for (i = 0; i < IEEE80211_HTRATE_MAXSIZE; i++)
			if (isset(htcap->hc_mcsset, i)) {
				for (j = i+1; j < IEEE80211_HTRATE_MAXSIZE; j++)
					if (isclr(htcap->hc_mcsset, j))
						break;
				j--;
				if (i == j)
					printf("%s%u", sep, i);
				else
					printf("%s%u-%u", sep, i, j);
				i += j-i;
				sep = ",";
			}
		printf("] extcap 0x%x txbf 0x%x antenna 0x%x>",
		    LE_READ_2(&htcap->hc_extcap),
		    LE_READ_4(&htcap->hc_txbf),
		    htcap->hc_antenna);
	}
}

static void
printhtinfo(const char *tag, const u_int8_t *ie, size_t ielen, int maxlen)
{
	printf("%s", tag);
	if (verbose) {
		const struct ieee80211_ie_htinfo *htinfo =
		    (const struct ieee80211_ie_htinfo *) ie;
		const char *sep;
		int i, j;

		printf("<ctl %u, %x,%x,%x,%x", htinfo->hi_ctrlchannel,
		    htinfo->hi_byte1, htinfo->hi_byte2, htinfo->hi_byte3,
		    LE_READ_2(&htinfo->hi_byte45));
		printf(" basicmcs[");
		sep = "";
		for (i = 0; i < IEEE80211_HTRATE_MAXSIZE; i++)
			if (isset(htinfo->hi_basicmcsset, i)) {
				for (j = i+1; j < IEEE80211_HTRATE_MAXSIZE; j++)
					if (isclr(htinfo->hi_basicmcsset, j))
						break;
				j--;
				if (i == j)
					printf("%s%u", sep, i);
				else
					printf("%s%u-%u", sep, i, j);
				i += j-i;
				sep = ",";
			}
		printf("]>");
	}
}

static void
printathie(const char *tag, const u_int8_t *ie, size_t ielen, int maxlen)
{

	printf("%s", tag);
	if (verbose) {
		const struct ieee80211_ath_ie *ath =
			(const struct ieee80211_ath_ie *)ie;

		printf("<");
		if (ath->ath_capability & ATHEROS_CAP_TURBO_PRIME)
			printf("DTURBO,");
		if (ath->ath_capability & ATHEROS_CAP_COMPRESSION)
			printf("COMP,");
		if (ath->ath_capability & ATHEROS_CAP_FAST_FRAME)
			printf("FF,");
		if (ath->ath_capability & ATHEROS_CAP_XR)
			printf("XR,");
		if (ath->ath_capability & ATHEROS_CAP_AR)
			printf("AR,");
		if (ath->ath_capability & ATHEROS_CAP_BURST)
			printf("BURST,");
		if (ath->ath_capability & ATHEROS_CAP_WME)
			printf("WME,");
		if (ath->ath_capability & ATHEROS_CAP_BOOST)
			printf("BOOST,");
		printf("0x%x>", LE_READ_2(ath->ath_defkeyix));
	}
}

static const char *
wpa_cipher(const u_int8_t *sel)
{
#define	WPA_SEL(x)	(((x)<<24)|WPA_OUI)
	u_int32_t w = LE_READ_4(sel);

	switch (w) {
	case WPA_SEL(WPA_CSE_NULL):
		return "NONE";
	case WPA_SEL(WPA_CSE_WEP40):
		return "WEP40";
	case WPA_SEL(WPA_CSE_WEP104):
		return "WEP104";
	case WPA_SEL(WPA_CSE_TKIP):
		return "TKIP";
	case WPA_SEL(WPA_CSE_CCMP):
		return "AES-CCMP";
	}
	return "?";		/* NB: so 1<< is discarded */
#undef WPA_SEL
}

static const char *
wpa_keymgmt(const u_int8_t *sel)
{
#define	WPA_SEL(x)	(((x)<<24)|WPA_OUI)
	u_int32_t w = LE_READ_4(sel);

	switch (w) {
	case WPA_SEL(WPA_ASE_8021X_UNSPEC):
		return "8021X-UNSPEC";
	case WPA_SEL(WPA_ASE_8021X_PSK):
		return "8021X-PSK";
	case WPA_SEL(WPA_ASE_NONE):
		return "NONE";
	}
	return "?";
#undef WPA_SEL
}

static void
printwpaie(const char *tag, const u_int8_t *ie, size_t ielen, int maxlen)
{
	u_int8_t len = ie[1];

	printf("%s", tag);
	if (verbose) {
		const char *sep;
		int n;

		ie += 6, len -= 4;		/* NB: len is payload only */

		printf("<v%u", LE_READ_2(ie));
		ie += 2, len -= 2;

		printf(" mc:%s", wpa_cipher(ie));
		ie += 4, len -= 4;

		/* unicast ciphers */
		n = LE_READ_2(ie);
		ie += 2, len -= 2;
		sep = " uc:";
		for (; n > 0; n--) {
			printf("%s%s", sep, wpa_cipher(ie));
			ie += 4, len -= 4;
			sep = "+";
		}

		/* key management algorithms */
		n = LE_READ_2(ie);
		ie += 2, len -= 2;
		sep = " km:";
		for (; n > 0; n--) {
			printf("%s%s", sep, wpa_keymgmt(ie));
			ie += 4, len -= 4;
			sep = "+";
		}

		if (len > 2)		/* optional capabilities */
			printf(", caps 0x%x", LE_READ_2(ie));
		printf(">");
	}
}

static const char *
rsn_cipher(const u_int8_t *sel)
{
#define	RSN_SEL(x)	(((x)<<24)|RSN_OUI)
	u_int32_t w = LE_READ_4(sel);

	switch (w) {
	case RSN_SEL(RSN_CSE_NULL):
		return "NONE";
	case RSN_SEL(RSN_CSE_WEP40):
		return "WEP40";
	case RSN_SEL(RSN_CSE_WEP104):
		return "WEP104";
	case RSN_SEL(RSN_CSE_TKIP):
		return "TKIP";
	case RSN_SEL(RSN_CSE_CCMP):
		return "AES-CCMP";
	case RSN_SEL(RSN_CSE_WRAP):
		return "AES-OCB";
	}
	return "?";
#undef WPA_SEL
}

static const char *
rsn_keymgmt(const u_int8_t *sel)
{
#define	RSN_SEL(x)	(((x)<<24)|RSN_OUI)
	u_int32_t w = LE_READ_4(sel);

	switch (w) {
	case RSN_SEL(RSN_ASE_8021X_UNSPEC):
		return "8021X-UNSPEC";
	case RSN_SEL(RSN_ASE_8021X_PSK):
		return "8021X-PSK";
	case RSN_SEL(RSN_ASE_NONE):
		return "NONE";
	}
	return "?";
#undef RSN_SEL
}

static void
printrsnie(const char *tag, const u_int8_t *ie, size_t ielen, int maxlen)
{
	printf("%s", tag);
	if (verbose) {
		const char *sep;
		int n;

		ie += 2, ielen -= 2;

		printf("<v%u", LE_READ_2(ie));
		ie += 2, ielen -= 2;

		printf(" mc:%s", rsn_cipher(ie));
		ie += 4, ielen -= 4;

		/* unicast ciphers */
		n = LE_READ_2(ie);
		ie += 2, ielen -= 2;
		sep = " uc:";
		for (; n > 0; n--) {
			printf("%s%s", sep, rsn_cipher(ie));
			ie += 4, ielen -= 4;
			sep = "+";
		}

		/* key management algorithms */
		n = LE_READ_2(ie);
		ie += 2, ielen -= 2;
		sep = " km:";
		for (; n > 0; n--) {
			printf("%s%s", sep, rsn_keymgmt(ie));
			ie += 4, ielen -= 4;
			sep = "+";
		}

		if (ielen > 2)		/* optional capabilities */
			printf(", caps 0x%x", LE_READ_2(ie));
		/* XXXPMKID */
		printf(">");
	}
}

/* XXX move to a public include file */
#define IEEE80211_WPS_DEV_PASS_ID	0x1012
#define IEEE80211_WPS_SELECTED_REG	0x1041
#define IEEE80211_WPS_SETUP_STATE	0x1044
#define IEEE80211_WPS_UUID_E		0x1047
#define IEEE80211_WPS_VERSION		0x104a

#define BE_READ_2(p)					\
	((u_int16_t)					\
	 ((((const u_int8_t *)(p))[1]      ) |		\
	  (((const u_int8_t *)(p))[0] <<  8)))

static void
printwpsie(const char *tag, const u_int8_t *ie, size_t ielen, int maxlen)
{
#define	N(a)	(sizeof(a) / sizeof(a[0]))
	u_int8_t len = ie[1];

	printf("%s", tag);
	if (verbose) {
		static const char *dev_pass_id[] = {
			"D",	/* Default (PIN) */
			"U",	/* User-specified */
			"M",	/* Machine-specified */
			"K",	/* Rekey */
			"P",	/* PushButton */
			"R"	/* Registrar-specified */
		};
		int n;

		ie +=6, len -= 4;		/* NB: len is payload only */

		/* WPS IE in Beacon and Probe Resp frames have different fields */
		printf("<");
		while (len) {
			uint16_t tlv_type = BE_READ_2(ie);
			uint16_t tlv_len  = BE_READ_2(ie + 2);

			ie += 4, len -= 4;

			switch (tlv_type) {
			case IEEE80211_WPS_VERSION:
				printf("v:%d.%d", *ie >> 4, *ie & 0xf);
				break;
			case IEEE80211_WPS_SETUP_STATE:
				/* Only 1 and 2 are valid */
				if (*ie == 0 || *ie >= 3)
					printf(" state:B");
				else
					printf(" st:%s", *ie == 1 ? "N" : "C");
				break;
			case IEEE80211_WPS_SELECTED_REG:
				printf(" sel:%s", *ie ? "T" : "F");
				break;
			case IEEE80211_WPS_DEV_PASS_ID:
				n = LE_READ_2(ie);
				if (n < N(dev_pass_id))
					printf(" dpi:%s", dev_pass_id[n]);
				break;
			case IEEE80211_WPS_UUID_E:
				printf(" uuid-e:");
				for (n = 0; n < (tlv_len - 1); n++)
					printf("%02x-", ie[n]);
				printf("%02x", ie[n]);
				break;
			}
			ie += tlv_len, len -= tlv_len;
		}
		printf(">");
	}
#undef N
}

static void
printtdmaie(const char *tag, const u_int8_t *ie, size_t ielen, int maxlen)
{
	printf("%s", tag);
	if (verbose && ielen >= sizeof(struct ieee80211_tdma_param)) {
		const struct ieee80211_tdma_param *tdma =
		   (const struct ieee80211_tdma_param *) ie;

		/* XXX tstamp */
		printf("<v%u slot:%u slotcnt:%u slotlen:%u bintval:%u inuse:0x%x>",
		    tdma->tdma_version, tdma->tdma_slot, tdma->tdma_slotcnt,
		    LE_READ_2(&tdma->tdma_slotlen), tdma->tdma_bintval,
		    tdma->tdma_inuse[0]);
	}
}

/*
 * Copy the ssid string contents into buf, truncating to fit.  If the
 * ssid is entirely printable then just copy intact.  Otherwise convert
 * to hexadecimal.  If the result is truncated then replace the last
 * three characters with "...".
 */
static int
copy_essid(char buf[], size_t bufsize, const u_int8_t *essid, size_t essid_len)
{
	const u_int8_t *p; 
	size_t maxlen;
	int i;

	if (essid_len > bufsize)
		maxlen = bufsize;
	else
		maxlen = essid_len;
	/* determine printable or not */
	for (i = 0, p = essid; i < maxlen; i++, p++) {
		if (*p < ' ' || *p > 0x7e)
			break;
	}
	if (i != maxlen) {		/* not printable, print as hex */
		if (bufsize < 3)
			return 0;
		strlcpy(buf, "0x", bufsize);
		bufsize -= 2;
		p = essid;
		for (i = 0; i < maxlen && bufsize >= 2; i++) {
			sprintf(&buf[2+2*i], "%02x", p[i]);
			bufsize -= 2;
		}
		if (i != essid_len)
			memcpy(&buf[2+2*i-3], "...", 3);
	} else {			/* printable, truncate as needed */
		memcpy(buf, essid, maxlen);
		if (maxlen != essid_len)
			memcpy(&buf[maxlen-3], "...", 3);
	}
	return maxlen;
}

static void
printssid(const char *tag, const u_int8_t *ie, size_t ielen, int maxlen)
{
	char ssid[2*IEEE80211_NWID_LEN+1];

	printf("%s<%.*s>", tag, copy_essid(ssid, maxlen, ie+2, ie[1]), ssid);
}

static void
printrates(const char *tag, const u_int8_t *ie, size_t ielen, int maxlen)
{
	const char *sep;
	int i;

	printf("%s", tag);
	sep = "<";
	for (i = 2; i < ielen; i++) {
		printf("%s%s%d", sep,
		    ie[i] & IEEE80211_RATE_BASIC ? "B" : "",
		    ie[i] & IEEE80211_RATE_VAL);
		sep = ",";
	}
	printf(">");
}

static void
printcountry(const char *tag, const u_int8_t *ie, size_t ielen, int maxlen)
{
	const struct ieee80211_country_ie *cie =
	   (const struct ieee80211_country_ie *) ie;
	int i, nbands, schan, nchan;

	printf("%s<%c%c%c", tag, cie->cc[0], cie->cc[1], cie->cc[2]);
	nbands = (cie->len - 3) / sizeof(cie->band[0]);
	for (i = 0; i < nbands; i++) {
		schan = cie->band[i].schan;
		nchan = cie->band[i].nchan;
		if (nchan != 1)
			printf(" %u-%u,%u", schan, schan + nchan-1,
			    cie->band[i].maxtxpwr);
		else
			printf(" %u,%u", schan, cie->band[i].maxtxpwr);
	}
	printf(">");
}

/* unaligned little endian access */     
#define LE_READ_4(p)					\
	((u_int32_t)					\
	 ((((const u_int8_t *)(p))[0]      ) |		\
	  (((const u_int8_t *)(p))[1] <<  8) |		\
	  (((const u_int8_t *)(p))[2] << 16) |		\
	  (((const u_int8_t *)(p))[3] << 24)))

static __inline int
iswpaoui(const u_int8_t *frm)
{
	return frm[1] > 3 && LE_READ_4(frm+2) == ((WPA_OUI_TYPE<<24)|WPA_OUI);
}

static __inline int
iswmeinfo(const u_int8_t *frm)
{
	return frm[1] > 5 && LE_READ_4(frm+2) == ((WME_OUI_TYPE<<24)|WME_OUI) &&
		frm[6] == WME_INFO_OUI_SUBTYPE;
}

static __inline int
iswmeparam(const u_int8_t *frm)
{
	return frm[1] > 5 && LE_READ_4(frm+2) == ((WME_OUI_TYPE<<24)|WME_OUI) &&
		frm[6] == WME_PARAM_OUI_SUBTYPE;
}

static __inline int
isatherosoui(const u_int8_t *frm)
{
	return frm[1] > 3 && LE_READ_4(frm+2) == ((ATH_OUI_TYPE<<24)|ATH_OUI);
}

static __inline int
istdmaoui(const uint8_t *frm)
{
	return frm[1] > 3 && LE_READ_4(frm+2) == ((TDMA_OUI_TYPE<<24)|TDMA_OUI);
}

static __inline int
iswpsoui(const uint8_t *frm)
{
	return frm[1] > 3 && LE_READ_4(frm+2) == ((WPS_OUI_TYPE<<24)|WPA_OUI);
}

static const char *
iename(int elemid)
{
	switch (elemid) {
	case IEEE80211_ELEMID_FHPARMS:	return " FHPARMS";
	case IEEE80211_ELEMID_CFPARMS:	return " CFPARMS";
	case IEEE80211_ELEMID_TIM:	return " TIM";
	case IEEE80211_ELEMID_IBSSPARMS:return " IBSSPARMS";
	case IEEE80211_ELEMID_CHALLENGE:return " CHALLENGE";
	case IEEE80211_ELEMID_PWRCNSTR:	return " PWRCNSTR";
	case IEEE80211_ELEMID_PWRCAP:	return " PWRCAP";
	case IEEE80211_ELEMID_TPCREQ:	return " TPCREQ";
	case IEEE80211_ELEMID_TPCREP:	return " TPCREP";
	case IEEE80211_ELEMID_SUPPCHAN:	return " SUPPCHAN";
	case IEEE80211_ELEMID_CHANSWITCHANN:return " CSA";
	case IEEE80211_ELEMID_MEASREQ:	return " MEASREQ";
	case IEEE80211_ELEMID_MEASREP:	return " MEASREP";
	case IEEE80211_ELEMID_QUIET:	return " QUIET";
	case IEEE80211_ELEMID_IBSSDFS:	return " IBSSDFS";
	case IEEE80211_ELEMID_TPC:	return " TPC";
	case IEEE80211_ELEMID_CCKM:	return " CCKM";
	}
	return " ???";
}

static void
printies(const u_int8_t *vp, int ielen, int maxcols)
{
	while (ielen > 0) {
		switch (vp[0]) {
		case IEEE80211_ELEMID_SSID:
			if (verbose)
				printssid(" SSID", vp, 2+vp[1], maxcols);
			break;
		case IEEE80211_ELEMID_RATES:
		case IEEE80211_ELEMID_XRATES:
			if (verbose)
				printrates(vp[0] == IEEE80211_ELEMID_RATES ?
				    " RATES" : " XRATES", vp, 2+vp[1], maxcols);
			break;
		case IEEE80211_ELEMID_DSPARMS:
			if (verbose)
				printf(" DSPARMS<%u>", vp[2]);
			break;
		case IEEE80211_ELEMID_COUNTRY:
			if (verbose)
				printcountry(" COUNTRY", vp, 2+vp[1], maxcols);
			break;
		case IEEE80211_ELEMID_ERP:
			if (verbose)
				printf(" ERP<0x%x>", vp[2]);
			break;
		case IEEE80211_ELEMID_VENDOR:
			if (iswpaoui(vp))
				printwpaie(" WPA", vp, 2+vp[1], maxcols);
			else if (iswmeinfo(vp))
				printwmeinfo(" WME", vp, 2+vp[1], maxcols);
			else if (iswmeparam(vp))
				printwmeparam(" WME", vp, 2+vp[1], maxcols);
			else if (isatherosoui(vp))
				printathie(" ATH", vp, 2+vp[1], maxcols);
			else if (iswpsoui(vp))
				printwpsie(" WPS", vp, 2+vp[1], maxcols);
			else if (istdmaoui(vp))
				printtdmaie(" TDMA", vp, 2+vp[1], maxcols);
			else if (verbose)
				printie(" VEN", vp, 2+vp[1], maxcols);
			break;
		case IEEE80211_ELEMID_RSN:
			printrsnie(" RSN", vp, 2+vp[1], maxcols);
			break;
		case IEEE80211_ELEMID_HTCAP:
			printhtcap(" HTCAP", vp, 2+vp[1], maxcols);
			break;
		case IEEE80211_ELEMID_HTINFO:
			if (verbose)
				printhtinfo(" HTINFO", vp, 2+vp[1], maxcols);
			break;
		default:
			if (verbose)
				printie(iename(vp[0]), vp, 2+vp[1], maxcols);
			break;
		}
		ielen -= 2+vp[1];
		vp += 2+vp[1];
	}
}

static void
printmimo(const struct ieee80211_mimo_info *mi)
{
	/* NB: don't muddy display unless there's something to show */
	if (mi->rssi[0] != 0 || mi->rssi[1] != 0 || mi->rssi[2] != 0) {
		/* XXX ignore EVM for now */
		printf(" (rssi %d:%d:%d nf %d:%d:%d)",
		    mi->rssi[0], mi->rssi[1], mi->rssi[2],
		    mi->noise[0], mi->noise[1], mi->noise[2]);
	}
}

static void
list_scan(int s)
{
	uint8_t buf[24*1024];
	char ssid[IEEE80211_NWID_LEN+1];
	const uint8_t *cp;
	int len, ssidmax;

	if (get80211len(s, IEEE80211_IOC_SCAN_RESULTS, buf, sizeof(buf), &len) < 0)
		errx(1, "unable to get scan results");
	if (len < sizeof(struct ieee80211req_scan_result))
		return;

	getchaninfo(s);

	ssidmax = verbose ? IEEE80211_NWID_LEN : 14;
	printf("%-*.*s  %-17.17s  %4s %4s  %-7s  %3s %4s\n"
		, ssidmax, ssidmax, "SSID"
		, "BSSID"
		, "CHAN"
		, "RATE"
		, " S:N"
		, "INT"
		, "CAPS"
	);
	cp = buf;
	do {
		const struct ieee80211req_scan_result *sr;
		const uint8_t *vp;

		sr = (const struct ieee80211req_scan_result *) cp;
		vp = cp + sr->isr_ie_off;
		printf("%-*.*s  %s  %3d  %3dM %3d:%-3d  %3d %-4.4s"
			, ssidmax
			  , copy_essid(ssid, ssidmax, vp, sr->isr_ssid_len)
			  , ssid
			, ether_ntoa((const struct ether_addr *) sr->isr_bssid)
			, ieee80211_mhz2ieee(sr->isr_freq, sr->isr_flags)
			, getmaxrate(sr->isr_rates, sr->isr_nrates)
			, (sr->isr_rssi/2)+sr->isr_noise, sr->isr_noise
			, sr->isr_intval
			, getcaps(sr->isr_capinfo)
		);
		printies(vp + sr->isr_ssid_len, sr->isr_ie_len, 24);
		printf("\n");
		cp += sr->isr_len, len -= sr->isr_len;
	} while (len >= sizeof(struct ieee80211req_scan_result));
}

#ifdef __FreeBSD__
#include <net80211/ieee80211_freebsd.h>
#endif
#ifdef __NetBSD__
#include <net80211/ieee80211_netbsd.h>
#endif

static void
scan_and_wait(int s)
{
	struct ieee80211_scan_req sr;
	struct ieee80211req ireq;
	int sroute;

	sroute = socket(PF_ROUTE, SOCK_RAW, 0);
	if (sroute < 0) {
		perror("socket(PF_ROUTE,SOCK_RAW)");
		return;
	}
	(void) memset(&ireq, 0, sizeof(ireq));
	(void) strncpy(ireq.i_name, name, sizeof(ireq.i_name));
	ireq.i_type = IEEE80211_IOC_SCAN_REQ;

	memset(&sr, 0, sizeof(sr));
	sr.sr_flags = IEEE80211_IOC_SCAN_ACTIVE
		    | IEEE80211_IOC_SCAN_NOPICK
		    | IEEE80211_IOC_SCAN_ONCE;
	sr.sr_duration = IEEE80211_IOC_SCAN_FOREVER;
	sr.sr_nssid = 0;

	ireq.i_data = &sr;
	ireq.i_len = sizeof(sr);
	/* NB: only root can trigger a scan so ignore errors */
	if (ioctl(s, SIOCS80211, &ireq) >= 0) {
		char buf[2048];
		struct if_announcemsghdr *ifan;
		struct rt_msghdr *rtm;

		do {
			if (read(sroute, buf, sizeof(buf)) < 0) {
				perror("read(PF_ROUTE)");
				break;
			}
			rtm = (struct rt_msghdr *) buf;
			if (rtm->rtm_version != RTM_VERSION)
				break;
			ifan = (struct if_announcemsghdr *) rtm;
		} while (rtm->rtm_type != RTM_IEEE80211 ||
		    ifan->ifan_what != RTM_IEEE80211_SCAN);
	}
	close(sroute);
}

static
DECL_CMD_FUNC(set80211scan, val, d)
{
	scan_and_wait(s);
	list_scan(s);
}

static enum ieee80211_opmode get80211opmode(int s);

static int
gettxseq(const struct ieee80211req_sta_info *si)
{
#define	IEEE80211_NODE_QOS	0x0002		/* QoS enabled */

	int i, txseq;

	if ((si->isi_state & IEEE80211_NODE_QOS) == 0)
		return si->isi_txseqs[0];
	/* XXX not right but usually what folks want */
	txseq = 0;
	for (i = 0; i < IEEE80211_TID_SIZE; i++)
		if (si->isi_txseqs[i] > txseq)
			txseq = si->isi_txseqs[i];
	return txseq;
#undef IEEE80211_NODE_QOS
}

static int
getrxseq(const struct ieee80211req_sta_info *si)
{
#define	IEEE80211_NODE_QOS	0x0002		/* QoS enabled */

	int i, rxseq;

	if ((si->isi_state & IEEE80211_NODE_QOS) == 0)
		return si->isi_rxseqs[0];
	/* XXX not right but usually what folks want */
	rxseq = 0;
	for (i = 0; i < IEEE80211_TID_SIZE; i++)
		if (si->isi_rxseqs[i] > rxseq)
			rxseq = si->isi_rxseqs[i];
	return rxseq;
#undef IEEE80211_NODE_QOS
}

static void
list_stations(int s)
{
	union {
		struct ieee80211req_sta_req req;
		uint8_t buf[24*1024];
	} u;
	enum ieee80211_opmode opmode = get80211opmode(s);
	const uint8_t *cp;
	int len;

	/* broadcast address =>'s get all stations */
	(void) memset(u.req.is_u.macaddr, 0xff, IEEE80211_ADDR_LEN);
	if (opmode == IEEE80211_M_STA) {
		/*
		 * Get information about the associated AP.
		 */
		(void) get80211(s, IEEE80211_IOC_BSSID,
		    u.req.is_u.macaddr, IEEE80211_ADDR_LEN);
	}
	if (get80211len(s, IEEE80211_IOC_STA_INFO, &u, sizeof(u), &len) < 0)
		errx(1, "unable to get station information");
	if (len < sizeof(struct ieee80211req_sta_info))
		return;

	getchaninfo(s);

	printf("%-17.17s %4s %4s %4s %4s %4s %6s %6s %4s %4s\n"
		, "ADDR"
		, "AID"
		, "CHAN"
		, "RATE"
		, "RSSI"
		, "IDLE"
		, "TXSEQ"
		, "RXSEQ"
		, "CAPS"
		, "FLAG"
	);
	cp = (const uint8_t *) u.req.info;
	do {
		const struct ieee80211req_sta_info *si;

		si = (const struct ieee80211req_sta_info *) cp;
		if (si->isi_len < sizeof(*si))
			break;
		printf("%s %4u %4d %3dM %3.1f %4d %6d %6d %-4.4s %-4.4s"
			, ether_ntoa((const struct ether_addr*) si->isi_macaddr)
			, IEEE80211_AID(si->isi_associd)
			, ieee80211_mhz2ieee(si->isi_freq, si->isi_flags)
			, si->isi_txmbps/2
			, si->isi_rssi/2.
			, si->isi_inact
			, gettxseq(si)
			, getrxseq(si)
			, getcaps(si->isi_capinfo)
			, getflags(si->isi_state)
		);
		printies(cp + si->isi_ie_off, si->isi_ie_len, 24);
		printmimo(&si->isi_mimo);
		printf("\n");
		cp += si->isi_len, len -= si->isi_len;
	} while (len >= sizeof(struct ieee80211req_sta_info));
}

static const char *
get_chaninfo(const struct ieee80211_channel *c, int precise,
	char buf[], size_t bsize)
{
	buf[0] = '\0';
	if (IEEE80211_IS_CHAN_FHSS(c))
		strlcat(buf, " FHSS", bsize);
	if (IEEE80211_IS_CHAN_A(c))
		strlcat(buf, " 11a", bsize);
	else if (IEEE80211_IS_CHAN_ANYG(c))
		strlcat(buf, " 11g", bsize);
	else if (IEEE80211_IS_CHAN_B(c))
		strlcat(buf, " 11b", bsize);
	if (IEEE80211_IS_CHAN_HALF(c))
		strlcat(buf, "/10Mhz", bsize);
	if (IEEE80211_IS_CHAN_QUARTER(c))
		strlcat(buf, "/5Mhz", bsize);
	if (IEEE80211_IS_CHAN_TURBO(c))
		strlcat(buf, " Turbo", bsize);
	if (precise) {
		if (IEEE80211_IS_CHAN_HT20(c))
			strlcat(buf, " ht/20", bsize);
		else if (IEEE80211_IS_CHAN_HT40D(c))
			strlcat(buf, " ht/40-", bsize);
		else if (IEEE80211_IS_CHAN_HT40U(c))
			strlcat(buf, " ht/40+", bsize);
	} else {
		if (IEEE80211_IS_CHAN_HT(c))
			strlcat(buf, " ht", bsize);
	}
	return buf;
}

static void
print_chaninfo(const struct ieee80211_channel *c, int verb)
{
	char buf[14];

	printf("Channel %3u : %u%c Mhz%-14.14s",
		ieee80211_mhz2ieee(c->ic_freq, c->ic_flags), c->ic_freq,
		IEEE80211_IS_CHAN_PASSIVE(c) ? '*' : ' ',
		get_chaninfo(c, verb, buf, sizeof(buf)));
}

static int
chanpref(const struct ieee80211_channel *c)
{
	if (IEEE80211_IS_CHAN_HT40(c))
		return 40;
	if (IEEE80211_IS_CHAN_HT20(c))
		return 30;
	if (IEEE80211_IS_CHAN_HALF(c))
		return 10;
	if (IEEE80211_IS_CHAN_QUARTER(c))
		return 5;
	if (IEEE80211_IS_CHAN_TURBO(c))
		return 25;
	if (IEEE80211_IS_CHAN_A(c))
		return 20;
	if (IEEE80211_IS_CHAN_G(c))
		return 20;
	if (IEEE80211_IS_CHAN_B(c))
		return 15;
	if (IEEE80211_IS_CHAN_PUREG(c))
		return 15;
	return 0;
}

static void
print_channels(int s, const struct ieee80211req_chaninfo *chans,
	int allchans, int verb)
{
	struct ieee80211req_chaninfo *achans;
	uint8_t reported[IEEE80211_CHAN_BYTES];
	const struct ieee80211_channel *c;
	int i, half;

	achans = malloc(IEEE80211_CHANINFO_SPACE(chans));
	if (achans == NULL)
		errx(1, "no space for active channel list");
	achans->ic_nchans = 0;
	memset(reported, 0, sizeof(reported));
	if (!allchans) {
		struct ieee80211req_chanlist active;

		if (get80211(s, IEEE80211_IOC_CHANLIST, &active, sizeof(active)) < 0)
			errx(1, "unable to get active channel list");
		for (i = 0; i < chans->ic_nchans; i++) {
			c = &chans->ic_chans[i];
			if (!isset(active.ic_channels, c->ic_ieee))
				continue;
			/*
			 * Suppress compatible duplicates unless
			 * verbose.  The kernel gives us it's
			 * complete channel list which has separate
			 * entries for 11g/11b and 11a/turbo.
			 */
			if (isset(reported, c->ic_ieee) && !verb) {
				/* XXX we assume duplicates are adjacent */
				achans->ic_chans[achans->ic_nchans-1] = *c;
			} else {
				achans->ic_chans[achans->ic_nchans++] = *c;
				setbit(reported, c->ic_ieee);
			}
		}
	} else {
		for (i = 0; i < chans->ic_nchans; i++) {
			c = &chans->ic_chans[i];
			/* suppress duplicates as above */
			if (isset(reported, c->ic_ieee) && !verb) {
				/* XXX we assume duplicates are adjacent */
				struct ieee80211_channel *a =
				    &achans->ic_chans[achans->ic_nchans-1];
				if (chanpref(c) > chanpref(a))
					*a = *c;
			} else {
				achans->ic_chans[achans->ic_nchans++] = *c;
				setbit(reported, c->ic_ieee);
			}
		}
	}
	half = achans->ic_nchans / 2;
	if (achans->ic_nchans % 2)
		half++;

	for (i = 0; i < achans->ic_nchans / 2; i++) {
		print_chaninfo(&achans->ic_chans[i], verb);
		print_chaninfo(&achans->ic_chans[half+i], verb);
		printf("\n");
	}
	if (achans->ic_nchans % 2) {
		print_chaninfo(&achans->ic_chans[i], verb);
		printf("\n");
	}
	free(achans);
}

static void
list_channels(int s, int allchans)
{
	getchaninfo(s);
	print_channels(s, chaninfo, allchans, verbose);
}

static void
print_txpow(const struct ieee80211_channel *c)
{
	printf("Channel %3u : %u Mhz %3.1f reg %2d  ",
	    c->ic_ieee, c->ic_freq,
	    c->ic_maxpower/2., c->ic_maxregpower);
}

static void
print_txpow_verbose(const struct ieee80211_channel *c)
{
	print_chaninfo(c, 1);
	printf("min %4.1f dBm  max %3.1f dBm  reg %2d dBm",
	    c->ic_minpower/2., c->ic_maxpower/2., c->ic_maxregpower);
	/* indicate where regulatory cap limits power use */
	if (c->ic_maxpower > 2*c->ic_maxregpower)
		printf(" <");
}

static void
list_txpow(int s)
{
	struct ieee80211req_chaninfo *achans;
	uint8_t reported[IEEE80211_CHAN_BYTES];
	struct ieee80211_channel *c, *prev;
	int i, half;

	getchaninfo(s);
	achans = malloc(IEEE80211_CHANINFO_SPACE(chaninfo));
	if (achans == NULL)
		errx(1, "no space for active channel list");
	achans->ic_nchans = 0;
	memset(reported, 0, sizeof(reported));
	for (i = 0; i < chaninfo->ic_nchans; i++) {
		c = &chaninfo->ic_chans[i];
		/* suppress duplicates as above */
		if (isset(reported, c->ic_ieee) && !verbose) {
			/* XXX we assume duplicates are adjacent */
			prev = &achans->ic_chans[achans->ic_nchans-1];
			/* display highest power on channel */
			if (c->ic_maxpower > prev->ic_maxpower)
				*prev = *c;
		} else {
			achans->ic_chans[achans->ic_nchans++] = *c;
			setbit(reported, c->ic_ieee);
		}
	}
	if (!verbose) {
		half = achans->ic_nchans / 2;
		if (achans->ic_nchans % 2)
			half++;

		for (i = 0; i < achans->ic_nchans / 2; i++) {
			print_txpow(&achans->ic_chans[i]);
			print_txpow(&achans->ic_chans[half+i]);
			printf("\n");
		}
		if (achans->ic_nchans % 2) {
			print_txpow(&achans->ic_chans[i]);
			printf("\n");
		}
	} else {
		for (i = 0; i < achans->ic_nchans; i++) {
			print_txpow_verbose(&achans->ic_chans[i]);
			printf("\n");
		}
	}
	free(achans);
}

static void
list_keys(int s)
{
}

#define	IEEE80211_C_BITS \
	"\20\1STA\7FF\10TURBOP\11IBSS\12PMGT" \
	"\13HOSTAP\14AHDEMO\15SWRETRY\16TXPMGT\17SHSLOT\20SHPREAMBLE" \
	"\21MONITOR\22DFS\30WPA1\31WPA2\32BURST\33WME\34WDS\36BGSCAN" \
	"\37TXFRAG\40TDMA"

static void
list_capabilities(int s)
{
	struct ieee80211_devcaps_req *dc;

	if (verbose)
		dc = malloc(IEEE80211_DEVCAPS_SIZE(MAXCHAN));
	else
		dc = malloc(IEEE80211_DEVCAPS_SIZE(1));
	if (dc == NULL)
		errx(1, "no space for device capabilities");
	dc->dc_chaninfo.ic_nchans = verbose ? MAXCHAN : 1;
	getdevcaps(s, dc);
	printb("drivercaps", dc->dc_drivercaps, IEEE80211_C_BITS);
	if (dc->dc_cryptocaps != 0 || verbose) {
		putchar('\n');
		printb("cryptocaps", dc->dc_cryptocaps, IEEE80211_CRYPTO_BITS);
	}
	if (dc->dc_htcaps != 0 || verbose) {
		putchar('\n');
		printb("htcaps", dc->dc_htcaps, IEEE80211_HTCAP_BITS);
	}
	putchar('\n');
	if (verbose) {
		chaninfo = &dc->dc_chaninfo;	/* XXX */
		print_channels(s, &dc->dc_chaninfo, 1/*allchans*/, verbose);
	}
	free(dc);
}

static int
get80211wme(int s, int param, int ac, int *val)
{
	struct ieee80211req ireq;

	(void) memset(&ireq, 0, sizeof(ireq));
	(void) strncpy(ireq.i_name, name, sizeof(ireq.i_name));
	ireq.i_type = param;
	ireq.i_len = ac;
	if (ioctl(s, SIOCG80211, &ireq) < 0) {
		warn("cannot get WME parameter %d, ac %d%s",
		    param, ac & IEEE80211_WMEPARAM_VAL,
		    ac & IEEE80211_WMEPARAM_BSS ? " (BSS)" : "");
		return -1;
	}
	*val = ireq.i_val;
	return 0;
}

static void
list_wme_aci(int s, const char *tag, int ac)
{
	int val;

	printf("\t%s", tag);

	/* show WME BSS parameters */
	if (get80211wme(s, IEEE80211_IOC_WME_CWMIN, ac, &val) != -1)
		printf(" cwmin %2u", val);
	if (get80211wme(s, IEEE80211_IOC_WME_CWMAX, ac, &val) != -1)
		printf(" cwmax %2u", val);
	if (get80211wme(s, IEEE80211_IOC_WME_AIFS, ac, &val) != -1)
		printf(" aifs %2u", val);
	if (get80211wme(s, IEEE80211_IOC_WME_TXOPLIMIT, ac, &val) != -1)
		printf(" txopLimit %3u", val);
	if (get80211wme(s, IEEE80211_IOC_WME_ACM, ac, &val) != -1) {
		if (val)
			printf(" acm");
		else if (verbose)
			printf(" -acm");
	}
	/* !BSS only */
	if ((ac & IEEE80211_WMEPARAM_BSS) == 0) {
		if (get80211wme(s, IEEE80211_IOC_WME_ACKPOLICY, ac, &val) != -1) {
			if (!val)
				printf(" -ack");
			else if (verbose)
				printf(" ack");
		}
	}
	printf("\n");
}

static void
list_wme(int s)
{
	static const char *acnames[] = { "AC_BE", "AC_BK", "AC_VI", "AC_VO" };
	int ac;

	if (verbose) {
		/* display both BSS and local settings */
		for (ac = WME_AC_BE; ac <= WME_AC_VO; ac++) {
	again:
			if (ac & IEEE80211_WMEPARAM_BSS)
				list_wme_aci(s, "     ", ac);
			else
				list_wme_aci(s, acnames[ac], ac);
			if ((ac & IEEE80211_WMEPARAM_BSS) == 0) {
				ac |= IEEE80211_WMEPARAM_BSS;
				goto again;
			} else
				ac &= ~IEEE80211_WMEPARAM_BSS;
		}
	} else {
		/* display only channel settings */
		for (ac = WME_AC_BE; ac <= WME_AC_VO; ac++)
			list_wme_aci(s, acnames[ac], ac);
	}
}

static void
list_roam(int s)
{
	const struct ieee80211_roamparam *rp;
	int mode;

	getroam(s);
	for (mode = IEEE80211_MODE_11A; mode < IEEE80211_MODE_11NA; mode++) {
		rp = &roamparams.params[mode];
		if (rp->rssi == 0 && rp->rate == 0)
			continue;
		if (rp->rssi & 1)
			LINE_CHECK("roam:%-6.6s rssi %2u.5dBm rate %2u Mb/s",
			    modename[mode], rp->rssi/2, rp->rate/2);
		else
			LINE_CHECK("roam:%-6.6s rssi %4udBm rate %2u Mb/s",
			    modename[mode], rp->rssi/2, rp->rate/2);
	}
	for (; mode < IEEE80211_MODE_MAX; mode++) {
		rp = &roamparams.params[mode];
		if (rp->rssi == 0 && rp->rate == 0)
			continue;
		if (rp->rssi & 1)
			LINE_CHECK("roam:%-6.6s rssi %2u.5dBm  MCS %2u    ",
			    modename[mode], rp->rssi/2, rp->rate &~ 0x80);
		else
			LINE_CHECK("roam:%-6.6s rssi %4udBm  MCS %2u    ",
			    modename[mode], rp->rssi/2, rp->rate &~ 0x80);
	}
}

static void
list_txparams(int s)
{
	const struct ieee80211_txparam *tp;
	int mode;

	gettxparams(s);
	for (mode = IEEE80211_MODE_11A; mode < IEEE80211_MODE_11NA; mode++) {
		tp = &txparams.params[mode];
		if (tp->mgmtrate == 0 && tp->mcastrate == 0)
			continue;
		if (tp->ucastrate == IEEE80211_FIXED_RATE_NONE)
			LINE_CHECK("%-6.6s ucast NONE    mgmt %2u Mb/s "
			    "mcast %2u Mb/s maxretry %u",
			    modename[mode], tp->mgmtrate/2,
			    tp->mcastrate/2, tp->maxretry);
		else
			LINE_CHECK("%-6.6s ucast %2u Mb/s mgmt %2u Mb/s "
			    "mcast %2u Mb/s maxretry %u",
			    modename[mode], tp->ucastrate/2, tp->mgmtrate/2,
			    tp->mcastrate/2, tp->maxretry);
	}
	for (; mode < IEEE80211_MODE_MAX; mode++) {
		tp = &txparams.params[mode];
		if (tp->mgmtrate == 0 && tp->mcastrate == 0)
			continue;
		if (tp->ucastrate == IEEE80211_FIXED_RATE_NONE)
			LINE_CHECK("%-6.6s ucast NONE    mgmt %2u MCS  "
			    "mcast %2u MCS  maxretry %u",
			    modename[mode], tp->mgmtrate &~ 0x80,
			    tp->mcastrate &~ 0x80, tp->maxretry);
		else
			LINE_CHECK("%-6.6s ucast %2u MCS  mgmt %2u MCS  "
			    "mcast %2u MCS  maxretry %u",
			    modename[mode], tp->ucastrate &~ 0x80,
			    tp->mgmtrate &~ 0x80,
			    tp->mcastrate &~ 0x80, tp->maxretry);
	}
}

static void
printpolicy(int policy)
{
	switch (policy) {
	case IEEE80211_MACCMD_POLICY_OPEN:
		printf("policy: open\n");
		break;
	case IEEE80211_MACCMD_POLICY_ALLOW:
		printf("policy: allow\n");
		break;
	case IEEE80211_MACCMD_POLICY_DENY:
		printf("policy: deny\n");
		break;
	case IEEE80211_MACCMD_POLICY_RADIUS:
		printf("policy: radius\n");
		break;
	default:
		printf("policy: unknown (%u)\n", policy);
		break;
	}
}

static void
list_mac(int s)
{
	struct ieee80211req ireq;
	struct ieee80211req_maclist *acllist;
	int i, nacls, policy, len;
	uint8_t *data;
	char c;

	(void) memset(&ireq, 0, sizeof(ireq));
	(void) strncpy(ireq.i_name, name, sizeof(ireq.i_name)); /* XXX ?? */
	ireq.i_type = IEEE80211_IOC_MACCMD;
	ireq.i_val = IEEE80211_MACCMD_POLICY;
	if (ioctl(s, SIOCG80211, &ireq) < 0) {
		if (errno == EINVAL) {
			printf("No acl policy loaded\n");
			return;
		}
		err(1, "unable to get mac policy");
	}
	policy = ireq.i_val;
	if (policy == IEEE80211_MACCMD_POLICY_OPEN) {
		c = '*';
	} else if (policy == IEEE80211_MACCMD_POLICY_ALLOW) {
		c = '+';
	} else if (policy == IEEE80211_MACCMD_POLICY_DENY) {
		c = '-';
	} else if (policy == IEEE80211_MACCMD_POLICY_RADIUS) {
		c = 'r';		/* NB: should never have entries */
	} else {
		printf("policy: unknown (%u)\n", policy);
		c = '?';
	}
	if (verbose || c == '?')
		printpolicy(policy);

	ireq.i_val = IEEE80211_MACCMD_LIST;
	ireq.i_len = 0;
	if (ioctl(s, SIOCG80211, &ireq) < 0)
		err(1, "unable to get mac acl list size");
	if (ireq.i_len == 0) {		/* NB: no acls */
		if (!(verbose || c == '?'))
			printpolicy(policy);
		return;
	}
	len = ireq.i_len;

	data = malloc(len);
	if (data == NULL)
		err(1, "out of memory for acl list");

	ireq.i_data = data;
	if (ioctl(s, SIOCG80211, &ireq) < 0)
		err(1, "unable to get mac acl list");
	nacls = len / sizeof(*acllist);
	acllist = (struct ieee80211req_maclist *) data;
	for (i = 0; i < nacls; i++)
		printf("%c%s\n", c, ether_ntoa(
			(const struct ether_addr *) acllist[i].ml_macaddr));
	free(data);
}

static void
print_regdomain(const struct ieee80211_regdomain *reg, int verb)
{
	if ((reg->regdomain != 0 &&
	    reg->regdomain != reg->country) || verb) {
		const struct regdomain *rd =
		    lib80211_regdomain_findbysku(getregdata(), reg->regdomain);
		if (rd == NULL)
			LINE_CHECK("regdomain %d", reg->regdomain);
		else
			LINE_CHECK("regdomain %s", rd->name);
	}
	if (reg->country != 0 || verb) {
		const struct country *cc =
		    lib80211_country_findbycc(getregdata(), reg->country);
		if (cc == NULL)
			LINE_CHECK("country %d", reg->country);
		else
			LINE_CHECK("country %s", cc->isoname);
	}
	if (reg->location == 'I')
		LINE_CHECK("indoor");
	else if (reg->location == 'O')
		LINE_CHECK("outdoor");
	else if (verb)
		LINE_CHECK("anywhere");
	if (reg->ecm)
		LINE_CHECK("ecm");
	else if (verb)
		LINE_CHECK("-ecm");
}

static void
list_regdomain(int s, int channelsalso)
{
	getregdomain(s);
	if (channelsalso) {
		getchaninfo(s);
		spacer = ':';
		print_regdomain(&regdomain, 1);
		LINE_BREAK();
		print_channels(s, chaninfo, 1/*allchans*/, 1/*verbose*/);
	} else
		print_regdomain(&regdomain, verbose);
}

static
DECL_CMD_FUNC(set80211list, arg, d)
{
#define	iseq(a,b)	(strncasecmp(a,b,sizeof(b)-1) == 0)

	LINE_INIT('\t');

	if (iseq(arg, "sta"))
		list_stations(s);
	else if (iseq(arg, "scan") || iseq(arg, "ap"))
		list_scan(s);
	else if (iseq(arg, "chan") || iseq(arg, "freq"))
		list_channels(s, 1);
	else if (iseq(arg, "active"))
		list_channels(s, 0);
	else if (iseq(arg, "keys"))
		list_keys(s);
	else if (iseq(arg, "caps"))
		list_capabilities(s);
	else if (iseq(arg, "wme") || iseq(arg, "wmm"))
		list_wme(s);
	else if (iseq(arg, "mac"))
		list_mac(s);
	else if (iseq(arg, "txpow"))
		list_txpow(s);
	else if (iseq(arg, "roam"))
		list_roam(s);
	else if (iseq(arg, "txparam") || iseq(arg, "txparm"))
		list_txparams(s);
	else if (iseq(arg, "regdomain"))
		list_regdomain(s, 1);
	else if (iseq(arg, "countries"))
		list_countries();
	else
		errx(1, "Don't know how to list %s for %s", arg, name);
	LINE_BREAK();
#undef iseq
}

static enum ieee80211_opmode
get80211opmode(int s)
{
	struct ifmediareq ifmr;

	(void) memset(&ifmr, 0, sizeof(ifmr));
	(void) strncpy(ifmr.ifm_name, name, sizeof(ifmr.ifm_name));

	if (ioctl(s, SIOCGIFMEDIA, (caddr_t)&ifmr) >= 0) {
		if (ifmr.ifm_current & IFM_IEEE80211_ADHOC) {
			if (ifmr.ifm_current & IFM_FLAG0)
				return IEEE80211_M_AHDEMO;
			else
				return IEEE80211_M_IBSS;
		}
		if (ifmr.ifm_current & IFM_IEEE80211_HOSTAP)
			return IEEE80211_M_HOSTAP;
		if (ifmr.ifm_current & IFM_IEEE80211_MONITOR)
			return IEEE80211_M_MONITOR;
	}
	return IEEE80211_M_STA;
}

#if 0
static void
printcipher(int s, struct ieee80211req *ireq, int keylenop)
{
	switch (ireq->i_val) {
	case IEEE80211_CIPHER_WEP:
		ireq->i_type = keylenop;
		if (ioctl(s, SIOCG80211, ireq) != -1)
			printf("WEP-%s", 
			    ireq->i_len <= 5 ? "40" :
			    ireq->i_len <= 13 ? "104" : "128");
		else
			printf("WEP");
		break;
	case IEEE80211_CIPHER_TKIP:
		printf("TKIP");
		break;
	case IEEE80211_CIPHER_AES_OCB:
		printf("AES-OCB");
		break;
	case IEEE80211_CIPHER_AES_CCM:
		printf("AES-CCM");
		break;
	case IEEE80211_CIPHER_CKIP:
		printf("CKIP");
		break;
	case IEEE80211_CIPHER_NONE:
		printf("NONE");
		break;
	default:
		printf("UNKNOWN (0x%x)", ireq->i_val);
		break;
	}
}
#endif

static void
printkey(const struct ieee80211req_key *ik)
{
	static const uint8_t zerodata[IEEE80211_KEYBUF_SIZE];
	int keylen = ik->ik_keylen;
	int printcontents;

	printcontents = printkeys &&
		(memcmp(ik->ik_keydata, zerodata, keylen) != 0 || verbose);
	if (printcontents)
		LINE_BREAK();
	switch (ik->ik_type) {
	case IEEE80211_CIPHER_WEP:
		/* compatibility */
		LINE_CHECK("wepkey %u:%s", ik->ik_keyix+1,
		    keylen <= 5 ? "40-bit" :
		    keylen <= 13 ? "104-bit" : "128-bit");
		break;
	case IEEE80211_CIPHER_TKIP:
		if (keylen > 128/8)
			keylen -= 128/8;	/* ignore MIC for now */
		LINE_CHECK("TKIP %u:%u-bit", ik->ik_keyix+1, 8*keylen);
		break;
	case IEEE80211_CIPHER_AES_OCB:
		LINE_CHECK("AES-OCB %u:%u-bit", ik->ik_keyix+1, 8*keylen);
		break;
	case IEEE80211_CIPHER_AES_CCM:
		LINE_CHECK("AES-CCM %u:%u-bit", ik->ik_keyix+1, 8*keylen);
		break;
	case IEEE80211_CIPHER_CKIP:
		LINE_CHECK("CKIP %u:%u-bit", ik->ik_keyix+1, 8*keylen);
		break;
	case IEEE80211_CIPHER_NONE:
		LINE_CHECK("NULL %u:%u-bit", ik->ik_keyix+1, 8*keylen);
		break;
	default:
		LINE_CHECK("UNKNOWN (0x%x) %u:%u-bit",
			ik->ik_type, ik->ik_keyix+1, 8*keylen);
		break;
	}
	if (printcontents) {
		int i;

		printf(" <");
		for (i = 0; i < keylen; i++)
			printf("%02x", ik->ik_keydata[i]);
		printf(">");
		if (ik->ik_type != IEEE80211_CIPHER_WEP &&
		    (ik->ik_keyrsc != 0 || verbose))
			printf(" rsc %ju", (uintmax_t)ik->ik_keyrsc);
		if (ik->ik_type != IEEE80211_CIPHER_WEP &&
		    (ik->ik_keytsc != 0 || verbose))
			printf(" tsc %ju", (uintmax_t)ik->ik_keytsc);
		if (ik->ik_flags != 0 && verbose) {
			const char *sep = " ";

			if (ik->ik_flags & IEEE80211_KEY_XMIT)
				printf("%stx", sep), sep = "+";
			if (ik->ik_flags & IEEE80211_KEY_RECV)
				printf("%srx", sep), sep = "+";
			if (ik->ik_flags & IEEE80211_KEY_DEFAULT)
				printf("%sdef", sep), sep = "+";
		}
		LINE_BREAK();
	}
}

static void
printrate(const char *tag, int v, int defrate, int defmcs)
{
	if (v == 11)
		LINE_CHECK("%s 5.5", tag);
	else if (v & 0x80) {
		if (v != defmcs)
			LINE_CHECK("%s %d", tag, v &~ 0x80);
	} else {
		if (v != defrate)
			LINE_CHECK("%s %d", tag, v/2);
	}
}

static int
getssid(int s, int ix, void *data, size_t len, int *plen)
{
	struct ieee80211req ireq;

	(void) memset(&ireq, 0, sizeof(ireq));
	(void) strncpy(ireq.i_name, name, sizeof(ireq.i_name));
	ireq.i_type = IEEE80211_IOC_SSID;
	ireq.i_val = ix;
	ireq.i_data = data;
	ireq.i_len = len;
	if (ioctl(s, SIOCG80211, &ireq) < 0)
		return -1;
	*plen = ireq.i_len;
	return 0;
}

static void
ieee80211_status(int s)
{
	static const uint8_t zerobssid[IEEE80211_ADDR_LEN];
	enum ieee80211_opmode opmode = get80211opmode(s);
	int i, num, wpa, wme, bgscan, bgscaninterval, val, len, wepmode;
	uint8_t data[32];
	const struct ieee80211_channel *c;
	const struct ieee80211_roamparam *rp;
	const struct ieee80211_txparam *tp;

	if (getssid(s, -1, data, sizeof(data), &len) < 0) {
		/* If we can't get the SSID, this isn't an 802.11 device. */
		return;
	}

	/*
	 * Invalidate cached state so printing status for multiple
	 * if's doesn't reuse the first interfaces' cached state.
	 */
	gotcurchan = 0;
	gotroam = 0;
	gottxparams = 0;
	gothtconf = 0;
	gotregdomain = 0;

	if (get80211val(s, IEEE80211_IOC_NUMSSIDS, &num) < 0)
		num = 0;
	printf("\tssid ");
	if (num > 1) {
		for (i = 0; i < num; i++) {
			if (getssid(s, i, data, sizeof(data), &len) >= 0 && len > 0) {
				printf(" %d:", i + 1);
				print_string(data, len);
			}
		}
	} else
		print_string(data, len);

	c = getcurchan(s);
	if (c->ic_freq != IEEE80211_CHAN_ANY) {
		char buf[14];
		printf(" channel %d (%u Mhz%s)", c->ic_ieee, c->ic_freq,
			get_chaninfo(c, 1, buf, sizeof(buf)));
	} else if (verbose)
		printf(" channel UNDEF");

	if (get80211(s, IEEE80211_IOC_BSSID, data, IEEE80211_ADDR_LEN) >= 0 &&
	    (memcmp(data, zerobssid, sizeof(zerobssid)) != 0 || verbose))
		printf(" bssid %s", ether_ntoa((struct ether_addr *)data));

	if (get80211len(s, IEEE80211_IOC_STATIONNAME, data, sizeof(data), &len) != -1) {
		printf("\n\tstationname ");
		print_string(data, len);
	}

	spacer = ' ';		/* force first break */
	LINE_BREAK();

	list_regdomain(s, 0);

	wpa = 0;
	if (get80211val(s, IEEE80211_IOC_AUTHMODE, &val) != -1) {
		switch (val) {
		case IEEE80211_AUTH_NONE:
			LINE_CHECK("authmode NONE");
			break;
		case IEEE80211_AUTH_OPEN:
			LINE_CHECK("authmode OPEN");
			break;
		case IEEE80211_AUTH_SHARED:
			LINE_CHECK("authmode SHARED");
			break;
		case IEEE80211_AUTH_8021X:
			LINE_CHECK("authmode 802.1x");
			break;
		case IEEE80211_AUTH_WPA:
			if (get80211val(s, IEEE80211_IOC_WPA, &wpa) < 0)
				wpa = 1;	/* default to WPA1 */
			switch (wpa) {
			case 2:
				LINE_CHECK("authmode WPA2/802.11i");
				break;
			case 3:
				LINE_CHECK("authmode WPA1+WPA2/802.11i");
				break;
			default:
				LINE_CHECK("authmode WPA");
				break;
			}
			break;
		case IEEE80211_AUTH_AUTO:
			LINE_CHECK("authmode AUTO");
			break;
		default:
			LINE_CHECK("authmode UNKNOWN (0x%x)", val);
			break;
		}
	}

	if (wpa || verbose) {
		if (get80211val(s, IEEE80211_IOC_WPS, &val) != -1) {
			if (val)
				LINE_CHECK("wps");
			else if (verbose)
				LINE_CHECK("-wps");
		}
		if (get80211val(s, IEEE80211_IOC_TSN, &val) != -1) {
			if (val)
				LINE_CHECK("tsn");
			else if (verbose)
				LINE_CHECK("-tsn");
		}
		if (ioctl(s, IEEE80211_IOC_COUNTERMEASURES, &val) != -1) {
			if (val)
				LINE_CHECK("countermeasures");
			else if (verbose)
				LINE_CHECK("-countermeasures");
		}
#if 0
		/* XXX not interesting with WPA done in user space */
		ireq.i_type = IEEE80211_IOC_KEYMGTALGS;
		if (ioctl(s, SIOCG80211, &ireq) != -1) {
		}

		ireq.i_type = IEEE80211_IOC_MCASTCIPHER;
		if (ioctl(s, SIOCG80211, &ireq) != -1) {
			LINE_CHECK("mcastcipher ");
			printcipher(s, &ireq, IEEE80211_IOC_MCASTKEYLEN);
			spacer = ' ';
		}

		ireq.i_type = IEEE80211_IOC_UCASTCIPHER;
		if (ioctl(s, SIOCG80211, &ireq) != -1) {
			LINE_CHECK("ucastcipher ");
			printcipher(s, &ireq, IEEE80211_IOC_UCASTKEYLEN);
		}

		if (wpa & 2) {
			ireq.i_type = IEEE80211_IOC_RSNCAPS;
			if (ioctl(s, SIOCG80211, &ireq) != -1) {
				LINE_CHECK("RSN caps 0x%x", ireq.i_val);
				spacer = ' ';
			}
		}

		ireq.i_type = IEEE80211_IOC_UCASTCIPHERS;
		if (ioctl(s, SIOCG80211, &ireq) != -1) {
		}
#endif
	}

	if (get80211val(s, IEEE80211_IOC_WEP, &wepmode) != -1 &&
	    wepmode != IEEE80211_WEP_NOSUP) {
		int firstkey;

		switch (wepmode) {
		case IEEE80211_WEP_OFF:
			LINE_CHECK("privacy OFF");
			break;
		case IEEE80211_WEP_ON:
			LINE_CHECK("privacy ON");
			break;
		case IEEE80211_WEP_MIXED:
			LINE_CHECK("privacy MIXED");
			break;
		default:
			LINE_CHECK("privacy UNKNOWN (0x%x)", wepmode);
			break;
		}

		/*
		 * If we get here then we've got WEP support so we need
		 * to print WEP status.
		 */

		if (get80211val(s, IEEE80211_IOC_WEPTXKEY, &val) < 0) {
			warn("WEP support, but no tx key!");
			goto end;
		}
		if (val != -1)
			LINE_CHECK("deftxkey %d", val+1);
		else if (wepmode != IEEE80211_WEP_OFF || verbose)
			LINE_CHECK("deftxkey UNDEF");

		if (get80211val(s, IEEE80211_IOC_NUMWEPKEYS, &num) < 0) {
			warn("WEP support, but no NUMWEPKEYS support!");
			goto end;
		}

		firstkey = 1;
		for (i = 0; i < num; i++) {
			struct ieee80211req_key ik;

			memset(&ik, 0, sizeof(ik));
			ik.ik_keyix = i;
			if (get80211(s, IEEE80211_IOC_WPAKEY, &ik, sizeof(ik)) < 0) {
				warn("WEP support, but can get keys!");
				goto end;
			}
			if (ik.ik_keylen != 0) {
				if (verbose)
					LINE_BREAK();
				printkey(&ik);
				firstkey = 0;
			}
		}
end:
		;
	}

	if (get80211val(s, IEEE80211_IOC_POWERSAVE, &val) != -1 &&
	    val != IEEE80211_POWERSAVE_NOSUP ) {
		if (val != IEEE80211_POWERSAVE_OFF || verbose) {
			switch (val) {
			case IEEE80211_POWERSAVE_OFF:
				LINE_CHECK("powersavemode OFF");
				break;
			case IEEE80211_POWERSAVE_CAM:
				LINE_CHECK("powersavemode CAM");
				break;
			case IEEE80211_POWERSAVE_PSP:
				LINE_CHECK("powersavemode PSP");
				break;
			case IEEE80211_POWERSAVE_PSP_CAM:
				LINE_CHECK("powersavemode PSP-CAM");
				break;
			}
			if (get80211val(s, IEEE80211_IOC_POWERSAVESLEEP, &val) != -1)
				LINE_CHECK("powersavesleep %d", val);
		}
	}

	if (get80211val(s, IEEE80211_IOC_TXPOWER, &val) != -1) {
		if (val & 1)
			LINE_CHECK("txpower %d.5", val/2);
		else
			LINE_CHECK("txpower %d", val/2);
	}
	if (verbose) {
		if (get80211val(s, IEEE80211_IOC_TXPOWMAX, &val) != -1)
			LINE_CHECK("txpowmax %.1f", val/2.);
	}

	if (get80211val(s, IEEE80211_IOC_DOTD, &val) != -1) {
		if (val)
			LINE_CHECK("dotd");
		else if (verbose)
			LINE_CHECK("-dotd");
	}

	if (get80211val(s, IEEE80211_IOC_RTSTHRESHOLD, &val) != -1) {
		if (val != IEEE80211_RTS_MAX || verbose)
			LINE_CHECK("rtsthreshold %d", val);
	}

	if (get80211val(s, IEEE80211_IOC_FRAGTHRESHOLD, &val) != -1) {
		if (val != IEEE80211_FRAG_MAX || verbose)
			LINE_CHECK("fragthreshold %d", val);
	}
	if (opmode == IEEE80211_M_STA || verbose) {
		if (get80211val(s, IEEE80211_IOC_BMISSTHRESHOLD, &val) != -1) {
			if (val != IEEE80211_HWBMISS_MAX || verbose)
				LINE_CHECK("bmiss %d", val);
		}
	}

	if (!verbose) {
		gettxparams(s);
		tp = &txparams.params[chan2mode(c)];
		printrate("ucastrate", tp->ucastrate,
		    IEEE80211_FIXED_RATE_NONE, IEEE80211_FIXED_RATE_NONE);
		printrate("mcastrate", tp->mcastrate, 2*1, 0x80|0);
		printrate("mgmtrate", tp->mgmtrate, 2*1, 0x80|0);
		if (tp->maxretry != 6)		/* XXX */
			LINE_CHECK("maxretry %d", tp->maxretry);
	} else {
		LINE_BREAK();
		list_txparams(s);
	}

	bgscaninterval = -1;
	(void) get80211val(s, IEEE80211_IOC_BGSCAN_INTERVAL, &bgscaninterval);

	if (get80211val(s, IEEE80211_IOC_SCANVALID, &val) != -1) {
		if (val != bgscaninterval || verbose)
			LINE_CHECK("scanvalid %u", val);
	}

	bgscan = 0;
	if (get80211val(s, IEEE80211_IOC_BGSCAN, &bgscan) != -1) {
		if (bgscan)
			LINE_CHECK("bgscan");
		else if (verbose)
			LINE_CHECK("-bgscan");
	}
	if (bgscan || verbose) {
		if (bgscaninterval != -1)
			LINE_CHECK("bgscanintvl %u", bgscaninterval);
		if (get80211val(s, IEEE80211_IOC_BGSCAN_IDLE, &val) != -1)
			LINE_CHECK("bgscanidle %u", val);
		if (!verbose) {
			getroam(s);
			rp = &roamparams.params[chan2mode(c)];
			if (rp->rssi & 1)
				LINE_CHECK("roam:rssi %u.5", rp->rssi/2);
			else
				LINE_CHECK("roam:rssi %u", rp->rssi/2);
			LINE_CHECK("roam:rate %u", rp->rate/2);
		} else {
			LINE_BREAK();
			list_roam(s);
		}
	}

	if (IEEE80211_IS_CHAN_ANYG(c) || verbose) {
		if (get80211val(s, IEEE80211_IOC_PUREG, &val) != -1) {
			if (val)
				LINE_CHECK("pureg");
			else if (verbose)
				LINE_CHECK("-pureg");
		}
		if (get80211val(s, IEEE80211_IOC_PROTMODE, &val) != -1) {
			switch (val) {
			case IEEE80211_PROTMODE_OFF:
				LINE_CHECK("protmode OFF");
				break;
			case IEEE80211_PROTMODE_CTS:
				LINE_CHECK("protmode CTS");
				break;
			case IEEE80211_PROTMODE_RTSCTS:
				LINE_CHECK("protmode RTSCTS");
				break;
			default:
				LINE_CHECK("protmode UNKNOWN (0x%x)", val);
				break;
			}
		}
	}

	if (IEEE80211_IS_CHAN_HT(c) || verbose) {
		gethtconf(s);
		switch (htconf & 3) {
		case 0:
		case 2:
			LINE_CHECK("-ht");
			break;
		case 1:
			LINE_CHECK("ht20");
			break;
		case 3:
			if (verbose)
				LINE_CHECK("ht");
			break;
		}
		if (get80211val(s, IEEE80211_IOC_HTCOMPAT, &val) != -1) {
			if (!val)
				LINE_CHECK("-htcompat");
			else if (verbose)
				LINE_CHECK("htcompat");
		}
		if (get80211val(s, IEEE80211_IOC_AMPDU, &val) != -1) {
			switch (val) {
			case 0:
				LINE_CHECK("-ampdu");
				break;
			case 1:
				LINE_CHECK("ampdutx -ampdurx");
				break;
			case 2:
				LINE_CHECK("-ampdutx ampdurx");
				break;
			case 3:
				if (verbose)
					LINE_CHECK("ampdu");
				break;
			}
		}
		if (get80211val(s, IEEE80211_IOC_AMPDU_LIMIT, &val) != -1) {
			switch (val) {
			case IEEE80211_HTCAP_MAXRXAMPDU_8K:
				LINE_CHECK("ampdulimit 8k");
				break;
			case IEEE80211_HTCAP_MAXRXAMPDU_16K:
				LINE_CHECK("ampdulimit 16k");
				break;
			case IEEE80211_HTCAP_MAXRXAMPDU_32K:
				LINE_CHECK("ampdulimit 32k");
				break;
			case IEEE80211_HTCAP_MAXRXAMPDU_64K:
				LINE_CHECK("ampdulimit 64k");
				break;
			}
		}
		if (get80211val(s, IEEE80211_IOC_AMPDU_DENSITY, &val) != -1) {
			switch (val) {
			case IEEE80211_HTCAP_MPDUDENSITY_NA:
				if (verbose)
					LINE_CHECK("ampdudensity NA");
				break;
			case IEEE80211_HTCAP_MPDUDENSITY_025:
				LINE_CHECK("ampdudensity .25");
				break;
			case IEEE80211_HTCAP_MPDUDENSITY_05:
				LINE_CHECK("ampdudensity .5");
				break;
			case IEEE80211_HTCAP_MPDUDENSITY_1:
				LINE_CHECK("ampdudensity 1");
				break;
			case IEEE80211_HTCAP_MPDUDENSITY_2:
				LINE_CHECK("ampdudensity 2");
				break;
			case IEEE80211_HTCAP_MPDUDENSITY_4:
				LINE_CHECK("ampdudensity 4");
				break;
			case IEEE80211_HTCAP_MPDUDENSITY_8:
				LINE_CHECK("ampdudensity 8");
				break;
			case IEEE80211_HTCAP_MPDUDENSITY_16:
				LINE_CHECK("ampdudensity 16");
				break;
			}
		}
		if (get80211val(s, IEEE80211_IOC_AMSDU, &val) != -1) {
			switch (val) {
			case 0:
				LINE_CHECK("-amsdu");
				break;
			case 1:
				LINE_CHECK("amsdutx -amsdurx");
				break;
			case 2:
				LINE_CHECK("-amsdutx amsdurx");
				break;
			case 3:
				if (verbose)
					LINE_CHECK("amsdu");
				break;
			}
		}
		/* XXX amsdu limit */
		if (get80211val(s, IEEE80211_IOC_SHORTGI, &val) != -1) {
			if (val)
				LINE_CHECK("shortgi");
			else if (verbose)
				LINE_CHECK("-shortgi");
		}
		if (get80211val(s, IEEE80211_IOC_HTPROTMODE, &val) != -1) {
			if (val == IEEE80211_PROTMODE_OFF)
				LINE_CHECK("htprotmode OFF");
			else if (val != IEEE80211_PROTMODE_RTSCTS)
				LINE_CHECK("htprotmode UNKNOWN (0x%x)", val);
			else if (verbose)
				LINE_CHECK("htprotmode RTSCTS");
		}
		if (get80211val(s, IEEE80211_IOC_PUREN, &val) != -1) {
			if (val)
				LINE_CHECK("puren");
			else if (verbose)
				LINE_CHECK("-puren");
		}
		if (get80211val(s, IEEE80211_IOC_SMPS, &val) != -1) {
			if (val == IEEE80211_HTCAP_SMPS_DYNAMIC)
				LINE_CHECK("smpsdyn");
			else if (val == IEEE80211_HTCAP_SMPS_ENA)
				LINE_CHECK("smps");
			else if (verbose)
				LINE_CHECK("-smps");
		}
		if (get80211val(s, IEEE80211_IOC_RIFS, &val) != -1) {
			if (val)
				LINE_CHECK("rifs");
			else if (verbose)
				LINE_CHECK("-rifs");
		}
	}

	if (get80211val(s, IEEE80211_IOC_WME, &wme) != -1) {
		if (wme)
			LINE_CHECK("wme");
		else if (verbose)
			LINE_CHECK("-wme");
	} else
		wme = 0;

	if (get80211val(s, IEEE80211_IOC_BURST, &val) != -1) {
		if (val)
			LINE_CHECK("burst");
		else if (verbose)
			LINE_CHECK("-burst");
	}

	if (get80211val(s, IEEE80211_IOC_FF, &val) != -1) {
		if (val)
			LINE_CHECK("ff");
		else if (verbose)
			LINE_CHECK("-ff");
	}
	if (get80211val(s, IEEE80211_IOC_TURBOP, &val) != -1) {
		if (val)
			LINE_CHECK("dturbo");
		else if (verbose)
			LINE_CHECK("-dturbo");
	}
	if (get80211val(s, IEEE80211_IOC_DWDS, &val) != -1) {
		if (val)
			LINE_CHECK("dwds");
		else if (verbose)
			LINE_CHECK("-dwds");
	}

	if (opmode == IEEE80211_M_HOSTAP) {
		if (get80211val(s, IEEE80211_IOC_HIDESSID, &val) != -1) {
			if (val)
				LINE_CHECK("hidessid");
			else if (verbose)
				LINE_CHECK("-hidessid");
		}
		if (get80211val(s, IEEE80211_IOC_APBRIDGE, &val) != -1) {
			if (!val)
				LINE_CHECK("-apbridge");
			else if (verbose)
				LINE_CHECK("apbridge");
		}
		if (get80211val(s, IEEE80211_IOC_DTIM_PERIOD, &val) != -1)
			LINE_CHECK("dtimperiod %u", val);

		if (get80211val(s, IEEE80211_IOC_DOTH, &val) != -1) {
			if (!val)
				LINE_CHECK("-doth");
			else if (verbose)
				LINE_CHECK("doth");
		}
		if (get80211val(s, IEEE80211_IOC_DFS, &val) != -1) {
			if (!val)
				LINE_CHECK("-dfs");
			else if (verbose)
				LINE_CHECK("dfs");
		}
		if (get80211val(s, IEEE80211_IOC_INACTIVITY, &val) != -1) {
			if (!val)
				LINE_CHECK("-inact");
			else if (verbose)
				LINE_CHECK("inact");
		}
	} else {
		if (get80211val(s, IEEE80211_IOC_ROAMING, &val) != -1) {
			if (val != IEEE80211_ROAMING_AUTO || verbose) {
				switch (val) {
				case IEEE80211_ROAMING_DEVICE:
					LINE_CHECK("roaming DEVICE");
					break;
				case IEEE80211_ROAMING_AUTO:
					LINE_CHECK("roaming AUTO");
					break;
				case IEEE80211_ROAMING_MANUAL:
					LINE_CHECK("roaming MANUAL");
					break;
				default:
					LINE_CHECK("roaming UNKNOWN (0x%x)",
						val);
					break;
				}
			}
		}
	}

	if (opmode == IEEE80211_M_AHDEMO) {
		if (get80211val(s, IEEE80211_IOC_TDMA_SLOT, &val) != -1)
			LINE_CHECK("tdmaslot %u", val);
		if (get80211val(s, IEEE80211_IOC_TDMA_SLOTCNT, &val) != -1)
			LINE_CHECK("tdmaslotcnt %u", val);
		if (get80211val(s, IEEE80211_IOC_TDMA_SLOTLEN, &val) != -1)
			LINE_CHECK("tdmaslotlen %u", val);
		if (get80211val(s, IEEE80211_IOC_TDMA_BINTERVAL, &val) != -1)
			LINE_CHECK("tdmabintval %u", val);
	} else if (get80211val(s, IEEE80211_IOC_BEACON_INTERVAL, &val) != -1) {
		/* XXX default define not visible */
		if (val != 100 || verbose)
			LINE_CHECK("bintval %u", val);
	}

	if (wme && verbose) {
		LINE_BREAK();
		list_wme(s);
	}
	LINE_BREAK();
}

static int
get80211(int s, int type, void *data, int len)
{
	struct ieee80211req ireq;

	(void) memset(&ireq, 0, sizeof(ireq));
	(void) strncpy(ireq.i_name, name, sizeof(ireq.i_name));
	ireq.i_type = type;
	ireq.i_data = data;
	ireq.i_len = len;
	return ioctl(s, SIOCG80211, &ireq);
}

static int
get80211len(int s, int type, void *data, int len, int *plen)
{
	struct ieee80211req ireq;

	(void) memset(&ireq, 0, sizeof(ireq));
	(void) strncpy(ireq.i_name, name, sizeof(ireq.i_name));
	ireq.i_type = type;
	ireq.i_len = len;
	assert(ireq.i_len == len);	/* NB: check for 16-bit truncation */
	ireq.i_data = data;
	if (ioctl(s, SIOCG80211, &ireq) < 0)
		return -1;
	*plen = ireq.i_len;
	return 0;
}

static int
get80211val(int s, int type, int *val)
{
	struct ieee80211req ireq;

	(void) memset(&ireq, 0, sizeof(ireq));
	(void) strncpy(ireq.i_name, name, sizeof(ireq.i_name));
	ireq.i_type = type;
	if (ioctl(s, SIOCG80211, &ireq) < 0)
		return -1;
	*val = ireq.i_val;
	return 0;
}

static void
set80211(int s, int type, int val, int len, void *data)
{
	struct ieee80211req	ireq;

	(void) memset(&ireq, 0, sizeof(ireq));
	(void) strncpy(ireq.i_name, name, sizeof(ireq.i_name));
	ireq.i_type = type;
	ireq.i_val = val;
	ireq.i_len = len;
	assert(ireq.i_len == len);	/* NB: check for 16-bit truncation */
	ireq.i_data = data;
	if (ioctl(s, SIOCS80211, &ireq) < 0)
		err(1, "SIOCS80211");
}

static const char *
get_string(const char *val, const char *sep, u_int8_t *buf, int *lenp)
{
	int len;
	int hexstr;
	u_int8_t *p;

	len = *lenp;
	p = buf;
	hexstr = (val[0] == '0' && tolower((u_char)val[1]) == 'x');
	if (hexstr)
		val += 2;
	for (;;) {
		if (*val == '\0')
			break;
		if (sep != NULL && strchr(sep, *val) != NULL) {
			val++;
			break;
		}
		if (hexstr) {
			if (!isxdigit((u_char)val[0])) {
				warnx("bad hexadecimal digits");
				return NULL;
			}
			if (!isxdigit((u_char)val[1])) {
				warnx("odd count hexadecimal digits");
				return NULL;
			}
		}
		if (p >= buf + len) {
			if (hexstr)
				warnx("hexadecimal digits too long");
			else
				warnx("string too long");
			return NULL;
		}
		if (hexstr) {
#define	tohex(x)	(isdigit(x) ? (x) - '0' : tolower(x) - 'a' + 10)
			*p++ = (tohex((u_char)val[0]) << 4) |
			    tohex((u_char)val[1]);
#undef tohex
			val += 2;
		} else
			*p++ = *val++;
	}
	len = p - buf;
	/* The string "-" is treated as the empty string. */
	if (!hexstr && len == 1 && buf[0] == '-') {
		len = 0;
		memset(buf, 0, *lenp);
	} else if (len < *lenp)
		memset(p, 0, *lenp - len);
	*lenp = len;
	return val;
}

static void
print_string(const u_int8_t *buf, int len)
{
	int i;
	int hasspc;

	i = 0;
	hasspc = 0;
	for (; i < len; i++) {
		if (!isprint(buf[i]) && buf[i] != '\0')
			break;
		if (isspace(buf[i]))
			hasspc++;
	}
	if (i == len) {
		if (hasspc || len == 0 || buf[0] == '\0')
			printf("\"%.*s\"", len, buf);
		else
			printf("%.*s", len, buf);
	} else {
		printf("0x");
		for (i = 0; i < len; i++)
			printf("%02x", buf[i]);
	}
}

/*
 * Virtual AP cloning support.
 */
static struct ieee80211_clone_params params = {
	.icp_opmode	= IEEE80211_M_STA,	/* default to station mode */
};

static void
wlan_create(int s, struct ifreq *ifr)
{
	static const uint8_t zerobssid[IEEE80211_ADDR_LEN];

	if (params.icp_parent[0] == '\0')
		errx(1, "must specify a parent when creating a wlan device");
	if (params.icp_opmode == IEEE80211_M_WDS &&
	    memcmp(params.icp_bssid, zerobssid, sizeof(zerobssid)) == 0)
		errx(1, "no bssid specified for WDS (use wlanbssid)");
	ifr->ifr_data = (caddr_t) &params;
	if (ioctl(s, SIOCIFCREATE2, ifr) < 0)
		err(1, "SIOCIFCREATE2");
}

static
DECL_CMD_FUNC(set80211clone_wlandev, arg, d)
{
	strlcpy(params.icp_parent, arg, IFNAMSIZ);
	clone_setcallback(wlan_create);
}

static
DECL_CMD_FUNC(set80211clone_wlanbssid, arg, d)
{
	const struct ether_addr *ea;

	ea = ether_aton(arg);
	if (ea == NULL)
		errx(1, "%s: cannot parse bssid", arg);
	memcpy(params.icp_bssid, ea->octet, IEEE80211_ADDR_LEN);
	clone_setcallback(wlan_create);
}

static
DECL_CMD_FUNC(set80211clone_wlanaddr, arg, d)
{
	const struct ether_addr *ea;

	ea = ether_aton(arg);
	if (ea == NULL)
		errx(1, "%s: cannot parse addres", arg);
	memcpy(params.icp_macaddr, ea->octet, IEEE80211_ADDR_LEN);
	params.icp_flags |= IEEE80211_CLONE_MACADDR;
	clone_setcallback(wlan_create);
}

static
DECL_CMD_FUNC(set80211clone_wlanmode, arg, d)
{
#define	iseq(a,b)	(strncasecmp(a,b,sizeof(b)-1) == 0)
	if (iseq(arg, "sta"))
		params.icp_opmode = IEEE80211_M_STA;
	else if (iseq(arg, "ahdemo") || iseq(arg, "adhoc-demo"))
		params.icp_opmode = IEEE80211_M_AHDEMO;
	else if (iseq(arg, "ibss") || iseq(arg, "adhoc"))
		params.icp_opmode = IEEE80211_M_IBSS;
	else if (iseq(arg, "ap") || iseq(arg, "host"))
		params.icp_opmode = IEEE80211_M_HOSTAP;
	else if (iseq(arg, "wds"))
		params.icp_opmode = IEEE80211_M_WDS;
	else if (iseq(arg, "monitor"))
		params.icp_opmode = IEEE80211_M_MONITOR;
	else if (iseq(arg, "tdma")) {
		params.icp_opmode = IEEE80211_M_AHDEMO;
		params.icp_flags |= IEEE80211_CLONE_TDMA;
	} else
		errx(1, "Don't know to create %s for %s", arg, name);
	clone_setcallback(wlan_create);
#undef iseq
}

static void
set80211clone_beacons(const char *val, int d, int s, const struct afswtch *rafp)
{
	/* NB: inverted sense */
	if (d)
		params.icp_flags &= ~IEEE80211_CLONE_NOBEACONS;
	else
		params.icp_flags |= IEEE80211_CLONE_NOBEACONS;
	clone_setcallback(wlan_create);
}

static void
set80211clone_bssid(const char *val, int d, int s, const struct afswtch *rafp)
{
	if (d)
		params.icp_flags |= IEEE80211_CLONE_BSSID;
	else
		params.icp_flags &= ~IEEE80211_CLONE_BSSID;
	clone_setcallback(wlan_create);
}

static void
set80211clone_wdslegacy(const char *val, int d, int s, const struct afswtch *rafp)
{
	if (d)
		params.icp_flags |= IEEE80211_CLONE_WDSLEGACY;
	else
		params.icp_flags &= ~IEEE80211_CLONE_WDSLEGACY;
	clone_setcallback(wlan_create);
}

static struct cmd ieee80211_cmds[] = {
	DEF_CMD_ARG("ssid",		set80211ssid),
	DEF_CMD_ARG("nwid",		set80211ssid),
	DEF_CMD_ARG("stationname",	set80211stationname),
	DEF_CMD_ARG("station",		set80211stationname),	/* BSD/OS */
	DEF_CMD_ARG("channel",		set80211channel),
	DEF_CMD_ARG("authmode",		set80211authmode),
	DEF_CMD_ARG("powersavemode",	set80211powersavemode),
	DEF_CMD("powersave",	1,	set80211powersave),
	DEF_CMD("-powersave",	0,	set80211powersave),
	DEF_CMD_ARG("powersavesleep", 	set80211powersavesleep),
	DEF_CMD_ARG("wepmode",		set80211wepmode),
	DEF_CMD("wep",		1,	set80211wep),
	DEF_CMD("-wep",		0,	set80211wep),
	DEF_CMD_ARG("deftxkey",		set80211weptxkey),
	DEF_CMD_ARG("weptxkey",		set80211weptxkey),
	DEF_CMD_ARG("wepkey",		set80211wepkey),
	DEF_CMD_ARG("nwkey",		set80211nwkey),		/* NetBSD */
	DEF_CMD("-nwkey",	0,	set80211wep),		/* NetBSD */
	DEF_CMD_ARG("rtsthreshold",	set80211rtsthreshold),
	DEF_CMD_ARG("protmode",		set80211protmode),
	DEF_CMD_ARG("txpower",		set80211txpower),
	DEF_CMD_ARG("roaming",		set80211roaming),
	DEF_CMD("wme",		1,	set80211wme),
	DEF_CMD("-wme",		0,	set80211wme),
	DEF_CMD("wmm",		1,	set80211wme),
	DEF_CMD("-wmm",		0,	set80211wme),
	DEF_CMD("hidessid",	1,	set80211hidessid),
	DEF_CMD("-hidessid",	0,	set80211hidessid),
	DEF_CMD("apbridge",	1,	set80211apbridge),
	DEF_CMD("-apbridge",	0,	set80211apbridge),
	DEF_CMD_ARG("chanlist",		set80211chanlist),
	DEF_CMD_ARG("bssid",		set80211bssid),
	DEF_CMD_ARG("ap",		set80211bssid),
	DEF_CMD("scan",	0,		set80211scan),
	DEF_CMD_ARG("list",		set80211list),
	DEF_CMD_ARG2("cwmin",		set80211cwmin),
	DEF_CMD_ARG2("cwmax",		set80211cwmax),
	DEF_CMD_ARG2("aifs",		set80211aifs),
	DEF_CMD_ARG2("txoplimit",	set80211txoplimit),
	DEF_CMD_ARG("acm",		set80211acm),
	DEF_CMD_ARG("-acm",		set80211noacm),
	DEF_CMD_ARG("ack",		set80211ackpolicy),
	DEF_CMD_ARG("-ack",		set80211noackpolicy),
	DEF_CMD_ARG2("bss:cwmin",	set80211bsscwmin),
	DEF_CMD_ARG2("bss:cwmax",	set80211bsscwmax),
	DEF_CMD_ARG2("bss:aifs",	set80211bssaifs),
	DEF_CMD_ARG2("bss:txoplimit",	set80211bsstxoplimit),
	DEF_CMD_ARG("dtimperiod",	set80211dtimperiod),
	DEF_CMD_ARG("bintval",		set80211bintval),
	DEF_CMD("mac:open",	IEEE80211_MACCMD_POLICY_OPEN,	set80211maccmd),
	DEF_CMD("mac:allow",	IEEE80211_MACCMD_POLICY_ALLOW,	set80211maccmd),
	DEF_CMD("mac:deny",	IEEE80211_MACCMD_POLICY_DENY,	set80211maccmd),
	DEF_CMD("mac:radius",	IEEE80211_MACCMD_POLICY_RADIUS,	set80211maccmd),
	DEF_CMD("mac:flush",	IEEE80211_MACCMD_FLUSH,		set80211maccmd),
	DEF_CMD("mac:detach",	IEEE80211_MACCMD_DETACH,	set80211maccmd),
	DEF_CMD_ARG("mac:add",		set80211addmac),
	DEF_CMD_ARG("mac:del",		set80211delmac),
	DEF_CMD_ARG("mac:kick",		set80211kickmac),
	DEF_CMD("pureg",	1,	set80211pureg),
	DEF_CMD("-pureg",	0,	set80211pureg),
	DEF_CMD("ff",		1,	set80211fastframes),
	DEF_CMD("-ff",		0,	set80211fastframes),
	DEF_CMD("dturbo",	1,	set80211dturbo),
	DEF_CMD("-dturbo",	0,	set80211dturbo),
	DEF_CMD("bgscan",	1,	set80211bgscan),
	DEF_CMD("-bgscan",	0,	set80211bgscan),
	DEF_CMD_ARG("bgscanidle",	set80211bgscanidle),
	DEF_CMD_ARG("bgscanintvl",	set80211bgscanintvl),
	DEF_CMD_ARG("scanvalid",	set80211scanvalid),
	DEF_CMD_ARG("roam:rssi",	set80211roamrssi),
	DEF_CMD_ARG("roam:rate",	set80211roamrate),
	DEF_CMD_ARG("mcastrate",	set80211mcastrate),
	DEF_CMD_ARG("ucastrate",	set80211ucastrate),
	DEF_CMD_ARG("mgtrate",		set80211mgtrate),
	DEF_CMD_ARG("mgmtrate",		set80211mgtrate),
	DEF_CMD_ARG("maxretry",		set80211maxretry),
	DEF_CMD_ARG("fragthreshold",	set80211fragthreshold),
	DEF_CMD("burst",	1,	set80211burst),
	DEF_CMD("-burst",	0,	set80211burst),
	DEF_CMD_ARG("bmiss",		set80211bmissthreshold),
	DEF_CMD_ARG("bmissthreshold",	set80211bmissthreshold),
	DEF_CMD("shortgi",	1,	set80211shortgi),
	DEF_CMD("-shortgi",	0,	set80211shortgi),
	DEF_CMD("ampdurx",	2,	set80211ampdu),
	DEF_CMD("-ampdurx",	-2,	set80211ampdu),
	DEF_CMD("ampdutx",	1,	set80211ampdu),
	DEF_CMD("-ampdutx",	-1,	set80211ampdu),
	DEF_CMD("ampdu",	3,	set80211ampdu),		/* NB: tx+rx */
	DEF_CMD("-ampdu",	-3,	set80211ampdu),
	DEF_CMD_ARG("ampdulimit",	set80211ampdulimit),
	DEF_CMD_ARG("ampdudensity",	set80211ampdudensity),
	DEF_CMD("amsdurx",	2,	set80211amsdu),
	DEF_CMD("-amsdurx",	-2,	set80211amsdu),
	DEF_CMD("amsdutx",	1,	set80211amsdu),
	DEF_CMD("-amsdutx",	-1,	set80211amsdu),
	DEF_CMD("amsdu",	3,	set80211amsdu),		/* NB: tx+rx */
	DEF_CMD("-amsdu",	-3,	set80211amsdu),
	DEF_CMD_ARG("amsdulimit",	set80211amsdulimit),
	DEF_CMD("puren",	1,	set80211puren),
	DEF_CMD("-puren",	0,	set80211puren),
	DEF_CMD("doth",		1,	set80211doth),
	DEF_CMD("-doth",	0,	set80211doth),
	DEF_CMD("dfs",		1,	set80211dfs),
	DEF_CMD("-dfs",		0,	set80211dfs),
	DEF_CMD("htcompat",	1,	set80211htcompat),
	DEF_CMD("-htcompat",	0,	set80211htcompat),
	DEF_CMD("dwds",		1,	set80211dwds),
	DEF_CMD("-dwds",	0,	set80211dwds),
	DEF_CMD("inact",	1,	set80211inact),
	DEF_CMD("-inact",	0,	set80211inact),
	DEF_CMD("tsn",		1,	set80211tsn),
	DEF_CMD("-tsn",		0,	set80211tsn),
	DEF_CMD_ARG("regdomain",	set80211regdomain),
	DEF_CMD_ARG("country",		set80211country),
	DEF_CMD("indoor",	'I',	set80211location),
	DEF_CMD("-indoor",	'O',	set80211location),
	DEF_CMD("outdoor",	'O',	set80211location),
	DEF_CMD("-outdoor",	'I',	set80211location),
	DEF_CMD("anywhere",	' ',	set80211location),
	DEF_CMD("ecm",		1,	set80211ecm),
	DEF_CMD("-ecm",		0,	set80211ecm),
	DEF_CMD("dotd",		1,	set80211dotd),
	DEF_CMD("-dotd",	0,	set80211dotd),
	DEF_CMD_ARG("htprotmode",	set80211htprotmode),
	DEF_CMD("ht20",		1,	set80211htconf),
	DEF_CMD("-ht20",	0,	set80211htconf),
	DEF_CMD("ht40",		3,	set80211htconf),	/* NB: 20+40 */
	DEF_CMD("-ht40",	0,	set80211htconf),
	DEF_CMD("ht",		3,	set80211htconf),	/* NB: 20+40 */
	DEF_CMD("-ht",		0,	set80211htconf),
	DEF_CMD("rifs",		1,	set80211rifs),
	DEF_CMD("-rifs",	0,	set80211rifs),
	DEF_CMD("smps",		IEEE80211_HTCAP_SMPS_ENA,	set80211smps),
	DEF_CMD("smpsdyn",	IEEE80211_HTCAP_SMPS_DYNAMIC,	set80211smps),
	DEF_CMD("-smps",	IEEE80211_HTCAP_SMPS_OFF,	set80211smps),
	/* XXX for testing */
	DEF_CMD_ARG("chanswitch",	set80211chanswitch),

	DEF_CMD_ARG("tdmaslot",		set80211tdmaslot),
	DEF_CMD_ARG("tdmaslotcnt",	set80211tdmaslotcnt),
	DEF_CMD_ARG("tdmaslotlen",	set80211tdmaslotlen),
	DEF_CMD_ARG("tdmabintval",	set80211tdmabintval),

	/* vap cloning support */
	DEF_CLONE_CMD_ARG("wlanaddr",	set80211clone_wlanaddr),
	DEF_CLONE_CMD_ARG("wlanbssid",	set80211clone_wlanbssid),
	DEF_CLONE_CMD_ARG("wlandev",	set80211clone_wlandev),
	DEF_CLONE_CMD_ARG("wlanmode",	set80211clone_wlanmode),
	DEF_CLONE_CMD("beacons", 1,	set80211clone_beacons),
	DEF_CLONE_CMD("-beacons", 0,	set80211clone_beacons),
	DEF_CLONE_CMD("bssid",	1,	set80211clone_bssid),
	DEF_CLONE_CMD("-bssid",	0,	set80211clone_bssid),
	DEF_CLONE_CMD("wdslegacy", 1,	set80211clone_wdslegacy),
	DEF_CLONE_CMD("-wdslegacy", 0,	set80211clone_wdslegacy),
};
static struct afswtch af_ieee80211 = {
	.af_name	= "af_ieee80211",
	.af_af		= AF_UNSPEC,
	.af_other_status = ieee80211_status,
};

static __constructor void
ieee80211_ctor(void)
{
#define	N(a)	(sizeof(a) / sizeof(a[0]))
	int i;

	for (i = 0; i < N(ieee80211_cmds);  i++)
		cmd_register(&ieee80211_cmds[i]);
	af_register(&af_ieee80211);
#undef N
}
