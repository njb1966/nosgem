/* ui.h -- Input handling stubs (C89)
 */

#ifndef NOS_UI_H
#define NOS_UI_H

/*
 * nos_ui_prompt -- print prompt and read a line into buf.
 * Returns 0 on success, -1 on error or EOF.
 */
int nos_ui_prompt(const char *prompt, char *buf, unsigned int max);

#endif /* NOS_UI_H */
