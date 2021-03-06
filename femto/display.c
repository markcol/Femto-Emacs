/*
 * display.c, Femto Emacs, Hugh Barney, Public Domain, 2016
 * Derived from: Anthony's Editor January 93, (Public Domain 1991, 1993 by Anthony Howe)
 */

#include "header.h"

/* Reverse scan for start of logical line containing offset */
point_t lnstart(buffer_t *bp, register point_t off)
{
	register char_t *p;
	do
		p = ptr(bp, --off);
	while (bp->b_buf < p && *p != '\n');
	return (bp->b_buf < p ? ++off : 0);
}

/*
 * Forward scan for start of logical line segment containing 'finish'.
 * A segment of a logical line corresponds to a physical screen line.
 */
point_t segstart(buffer_t *bp, point_t start, point_t finish)
{
	char_t *p;
	int c = 0;
	point_t scan = start;

	while (scan < finish) {
		p = ptr(bp, scan);
		if (*p == '\n') {
			c = 0;
			start = scan+1;
		} else if (COLS <= c) {
			c = 0;
			start = scan;
		}
		++scan;
		c += *p == '\t' ? 8 - (c & 7) : 1;
	}
	return (c < COLS ? start : finish);
}

/* Forward scan for start of logical line segment following 'finish' */
point_t segnext(buffer_t *bp, point_t start, point_t finish)
{
	char_t *p;
	int c = 0;

	point_t scan = segstart(bp, start, finish);
	for (;;) {
		p = ptr(bp, scan);
		if (bp->b_ebuf <= p || COLS <= c)
			break;
		++scan;
		if (*p == '\n')
			break;
		c += *p == '\t' ? 8 - (c & 7) : 1;
	}
	return (p < bp->b_ebuf ? scan : pos(bp, bp->b_ebuf));
}

/* Move up one screen line */
point_t upup(buffer_t *bp, point_t off)
{
	point_t curr = lnstart(bp, off);
	point_t seg = segstart(bp, curr, off);
	if (curr < seg)
		off = segstart(bp, curr, seg-1);
	else
		off = segstart(bp, lnstart(bp,curr-1), curr-1);
	return (off);
}

/* Move down one screen line */
point_t dndn(buffer_t *bp, point_t off)
{
	return (segnext(bp, lnstart(bp,off), off));
}

/* Return the offset of a column on the specified line */
point_t lncolumn(buffer_t *bp, point_t offset, int column)
{
	int c = 0;
	char_t *p;
	while ((p = ptr(bp, offset)) < bp->b_ebuf && *p != '\n' && c < column) {
		c += *p == '\t' ? 8 - (c & 7) : 1;
		++offset;
	}
	return (offset);
}

void display_char(buffer_t *bp, char_t *p)
{
	if ((ptr(bp, bp->b_mark) == p) && (bp->b_mark != NOMARK)) {
		addch(*p | A_REVERSE);
	} else if (pos(bp,p) == bp->b_point && bp->b_paren != NOPAREN) {
		attron(COLOR_PAIR(3));
		addch(*p);
		attron(COLOR_PAIR(1));
	} else if (bp->b_paren != NOPAREN && pos(bp,p) == bp->b_paren) { 
		attron(COLOR_PAIR(3));
		addch(*p);
		attron(COLOR_PAIR(1));
	} else {
		addch(*p);
	}
}

void display(window_t *wp, int flag)
{
	char_t *p;
	int i, j, k, nch;
	buffer_t *bp = wp->w_bufp;

	/* find start of screen, handle scroll up off page or top of file  */
	/* point is always within b_page and b_epage */
	if (bp->b_point < bp->b_page)
		bp->b_page = segstart(bp, lnstart(bp,bp->b_point), bp->b_point);

	/* reframe when scrolled off bottom */
	if (bp->b_epage <= bp->b_point) {
		/* Find end of screen plus one. */
		bp->b_page = dndn(bp, bp->b_point);
		/* if we scoll to EOF we show 1 blank line at bottom of screen */
		if (pos(bp, bp->b_ebuf) <= bp->b_page) {
			bp->b_page = pos(bp, bp->b_ebuf);
			i = wp->w_rows - 1;
		} else {
			i = wp->w_rows - 0;
		}
		/* Scan backwards the required number of lines. */
		while (0 < i--)
			bp->b_page = upup(bp, bp->b_page);
	}

	move(wp->w_top, 0); /* start from top of window */
	i = wp->w_top;
	j = 0;
	bp->b_epage = bp->b_page;
	/* paint screen from top of page until we hit maxline */ 
	while (1) {
		/* reached point - store the cursor position */
		if (bp->b_point == bp->b_epage) {
			bp->b_row = i;
			bp->b_col = j;
		}
		p = ptr(bp, bp->b_epage);
		nch = 1;
		if (wp->w_top + wp->w_rows <= i || bp->b_ebuf <= p) /* maxline */
			break;
		if (*p != '\r') {
			nch = utf8_size(*p);
			if ( nch > 1) {
				j++;
				display_utf8(bp, *p, nch);
			}
			else if (isprint(*p) || *p == '\t' || *p == '\n') {
				j += *p == '\t' ? 8-(j&7) : 1;
				display_char(bp, p);
			} else {
				const char *ctrl = unctrl(*p);
				j += (int) strlen(ctrl);
				addstr(ctrl);
			}
		}
		if (*p == '\n' || COLS <= j) {
			j -= COLS;
			if (j < 0)
				j = 0;
			++i;
		}
		bp->b_epage = bp->b_epage + nch;
	}

	/* replacement for clrtobot() to bottom of window */
	for (k=i; k < wp->w_top + wp->w_rows; k++) {
		move(k, j); /* clear from very last char not start of line */
		clrtoeol();
		j = 0; /* thereafter start of line */
	}

	b2w(wp); /* save buffer stuff on window */
	modeline(wp);
	if (wp == curwp && flag) {
		dispmsg();
		move(bp->b_row, bp->b_col); /* set cursor */
		refresh();
	}
	wp->w_update = FALSE;
}

/* work out number of bytes based on first byte */
int utf8_size(char_t c)
{
	if (c >= 192 && c < 224) {
		return 2; // 2 top bits set mean 2 byte utf8 char
	} else if (c >= 224 && c < 240) {
		return 3; // 3 top bits set mean 3 byte utf8 char
	} else if (c >= 240) {
		return 4; // 4 top bits set mean 4 byte utf8 char
	}

	return 1; /* if in doubt it is 1 */
}

void display_utf8(buffer_t *bp, char_t c, int n)
{
	char sbuf[6];
	int i = 0;
	
	for (i=0; i<n; i++) {
		sbuf[i] = *ptr(bp, bp->b_epage + i);
	}
	sbuf[n] = '\0';	
	addstr(sbuf);
}

void modeline(window_t *wp)
{
	int i;
	char lch, mch, och;

	/* n = utf8_size(*(ptr(wp->w_bufp, wp->w_bufp->b_point))); */
	attron(COLOR_PAIR(2));
	move(wp->w_top + wp->w_rows, 0);
	lch = (wp == curwp ? '=' : '-');
	mch = ((wp->w_bufp->b_flags & B_MODIFIED) ? '*' : lch);
	och = ((wp->w_bufp->b_flags & B_OVERWRITE) ? 'O' : lch);

	/* debug version */
	/* sprintf(temp, "%c%c%c Femto: %c%c %s %s  T%dR%d Pt%ld Pg%ld Pe%ld r%dc%d B%d",  lch,och,mch,lch,lch, wp->w_name, get_buffer_name(wp->w_bufp), wp->w_top, wp->w_rows, wp->w_point, wp->w_bufp->b_page, wp->w_bufp->b_epage, wp->w_bufp->b_row, wp->w_bufp->b_col, wp->w_bufp->b_cnt); */

	/* sprintf(temp, "%c%c%c Femto: %c%c %s %s  T%dR%d Pt%ld Pg%ld Pe%ld r%dc%d B%d N%d",  lch,och,mch,lch,lch, wp->w_name, get_buffer_name(wp->w_bufp), wp->w_top, wp->w_rows, wp->w_point, wp->w_bufp->b_page, wp->w_bufp->b_epage, wp->w_bufp->b_row, wp->w_bufp->b_col, wp->w_bufp->b_cnt, n); */
	
	sprintf(temp, "%c%c%c Femto: %c%c %s",  lch,och,mch,lch,lch, get_buffer_name(wp->w_bufp));
	addstr(temp);

	for (i = strlen(temp) + 1; i <= COLS; i++)
		addch(lch);
	attron(COLOR_PAIR(1));
}

void dispmsg()
{
	move(MSGLINE, 0);
	if (msgflag) {
		addstr(msgline);
		msgflag = FALSE;
	}
	clrtoeol();
}

void display_prompt_and_response(char *prompt, char *response)
{
	mvaddstr(MSGLINE, 0, prompt);
	/* if we have a value print it and go to end of it */
	if (response[0] != '\0')
		addstr(response);
	clrtoeol();
}

void update_display()
{   
	window_t *wp;
	buffer_t *bp;

	bp = curwp->w_bufp;
	bp->b_cpoint = bp->b_point; /* cpoint only ever set here */
	
	/* only one window */
	if (wheadp->w_next == NULL) {
		display(curwp, TRUE);
		refresh();
		bp->b_psize = bp->b_size;
		return;
	}

	display(curwp, FALSE); /* this is key, we must call our win first to get accurate page and epage etc */
	
	/* never curwp,  but same buffer in different window or update flag set*/
	for (wp=wheadp; wp != NULL; wp = wp->w_next) {
		if (wp != curwp && (wp->w_bufp == bp || wp->w_update)) {
			w2b(wp);
			display(wp, FALSE);
		}
	}

	/* now display our window and buffer */
	w2b(curwp);
	dispmsg();
	move(curwp->w_row, curwp->w_col); /* set cursor for curwp */
	refresh();
	bp->b_psize = bp->b_size;  /* now safe to save previous size for next time */
}

void w2b(window_t *w)
{
	w->w_bufp->b_point = w->w_point;
	w->w_bufp->b_page = w->w_page;
	w->w_bufp->b_epage = w->w_epage;
	w->w_bufp->b_row = w->w_row;
	w->w_bufp->b_col = w->w_col;
	
	/* fixup pointers in other windows of the same buffer, if size of edit text changed */
	if (w->w_bufp->b_point > w->w_bufp->b_cpoint) {
		w->w_bufp->b_point += (w->w_bufp->b_size - w->w_bufp->b_psize);
		w->w_bufp->b_page += (w->w_bufp->b_size - w->w_bufp->b_psize);
		w->w_bufp->b_epage += (w->w_bufp->b_size - w->w_bufp->b_psize);
	}
}

void b2w(window_t *w)
{
	w->w_point = w->w_bufp->b_point;
	w->w_page = w->w_bufp->b_page;
	w->w_epage = w->w_bufp->b_epage;
	w->w_row = w->w_bufp->b_row;
	w->w_col = w->w_bufp->b_col;
	w->w_bufp->b_size = (w->w_bufp->b_ebuf - w->w_bufp->b_buf) - (w->w_bufp->b_egap - w->w_bufp->b_gap);
}
