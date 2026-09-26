#pragma once
/* Dashboard only; no inbound networking. Actions require physical UI selection. */
int xv_release_action(int action); /* 0 check, 1 download, 2 install, 3 rollback */
int xv_release_busy(void);
void xv_release_status(char *out, unsigned size);
void xv_release_detail(char *out, unsigned size);
