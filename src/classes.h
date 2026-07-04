/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Leonardo Roman da Rosa */
/* classes.h - the class picker (classes.c, banked with nexthack.c). */
#ifndef CLASSES_H
#define CLASSES_H

#define NCLASS 4

void pick_class(void) __banked;         /* menu; fills the character sheet   */
void give_kit(void) __banked;           /* starting gear + gold (post-reset) */
const char *class_name(void) __banked;  /* display name; SAME-BANK callers only */

#endif /* CLASSES_H */
