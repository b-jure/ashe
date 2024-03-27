#include "acommon.h"
#include "ainput.h"
#include "autils.h"
#include "adbg.h"
#include "ashell.h" /* avoid recursive include in 'ainput.h' */

#include <stdio.h>
#include <errno.h>
#include <dirent.h>

const char *logfiles[] = {
	NULL,
	NULL,
};

/* 
 * TIP:
 * If you are using neovim/vim one nice thing to have is open
 * two terminals in a split view (either using tmux, window 
 * manager, vim, etc..) one will contain your 'logfile.txt'
 * file while the other will be ashe executable.
 * Now add this to your init.lua file (neovim):
 * ```
 * vim.o.updatetime = 1000 -- 1 second
 * vim.api.nvim_create_autocmd({"CursorHold", "BufRead"}, {
 * 	pattern = {"*dbginfo.txt"}, -- only for debug output files
 * 	command = "checktime | call feedkeys('G')",
 * })
 * ```
 * This will update your '.txt' files each second if
 * they get updated without you needing to hover or make
 * action inside the vim buffer, this way you can type freely
 * in ashe while testing without navigating to the debug file.
 */

/* Debug cursor position */
ASHE_PUBLIC void debug_cursor(void)
{
	static char buffer[2048];
	FILE *fp = NULL;
	memmax len = 0;
	ssize temp = 0;

	errno = 0;
	if ((fp = fopen(logfiles[ALOG_CURSOR], "a")) == NULL)
		goto error;
	temp = snprintf(buffer, sizeof(buffer),
			"[ROW:%u][COL:%u][LINE_LEN:%lu][IBFIDX:%lu]\r\n", ROW,
			COL, LINE.len, IBFIDX);
	if (temp < 0 || temp > BUFSIZ)
		goto error;
	len += temp;
	if (fwrite(buffer, sizeof(byte), len, fp) != len)
		goto error;
	if (fclose(fp) == EOF)
		goto error;
	return;
error:
	fclose(fp);
	print_errno();
	panic("panic in debug_cursor");
}

// clang-format off
/* Debug each row and its line (contents) */
ASHE_PUBLIC void debug_lines(void)
{
	static char temp[128];
	Buffer buffer;
	memmax len = 0;
	uint32 i = 0;
	uint32 idx;
	FILE *fp;

	errno = 0;
	Buffer_init(&buffer);
	if ((fp = fopen(logfiles[ALOG_LINES], "w")) == NULL)
		goto error;
	for (i = 0; i < LINES.len; i++) {
		if ((len = snprintf(temp, sizeof(temp), "[LINE:%u][LEN:%lu] \"",
				i, LINES.data[i].len)) < 0 ||
				len > sizeof(temp)) 
			goto error;
		Buffer_push_str(&buffer, temp, len);
		idx = buffer.len;
		Buffer_push_str(&buffer, LINES.data[i].start, LINES.data[i].len);
		unescape(&buffer, idx, buffer.len);
		Buffer_push_str(&buffer, "\"\r\n", sizeof("\"\r\n") - 1);
	}
	if (fwrite(buffer.data, sizeof(byte), buffer.len, fp) < buffer.len)
		goto error;
	if (fclose(fp) == EOF)
		goto fclose_error;
	Buffer_free(&buffer, NULL);
	return;
error:
	fclose(fp);
fclose_error:
	Buffer_free(&buffer, NULL);
	print_errno();
	panic("panic in debug_lines()");
}
// clang-format on

ASHE_PUBLIC void remove_logfiles(void)
{
	DIR *root;
	struct dirent *entry;

	errno = 0;
	if ((root = opendir(".")) == NULL)
		goto error;
	for (errno = 0; (entry = readdir(root)) != NULL; errno = 0) {
		if ((strcmp(entry->d_name, logfiles[ALOG_CURSOR]) == 0 ||
		     strcmp(entry->d_name, logfiles[ALOG_LINES]) == 0) &&
		    entry->d_type == DT_REG) {
			if (unlink(entry->d_name) < 0)
				print_errno(); /* still try unlink other logfiles */
		}
	}
	if (errno != 0)
		goto error;
	closedir(root);
	return;
error:
	closedir(root);
	print_errno();
	panic("panic in remove_logfiles()");
}

ASHE_PUBLIC void logfile_create(const char *logfile, int32 which)
{
	logfiles[which] = logfile;
	if (fopen(logfile, "w") == NULL) {
		print_errno();
		panic("panic in logfile_create(\"%s\")", logfile);
	}
}
