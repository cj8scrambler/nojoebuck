#ifndef __UI_SERVER_H
#define __UI_SERVER_H

int ui_init(buffer_config_t *bc);
int ui_cleanup(void);
void *ui_server_thread(void *data);

#endif
