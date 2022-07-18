#include <yed/plugin.h>
#include <time.h>

#define IS_ROOT   -1
#define IS_FILE    0
#define IS_DIR     1
#define IS_IMAGE   2
#define IS_ARCHIVE 3
#define IS_LINK    4
#define IS_B_LINK  5
#define IS_DEVICE  6
#define IS_EXEC    7
#define MAYBE_CONVERT(rgb) (tc ? (rgb) : rgb_to_256(rgb))

/* global structs */
typedef struct {
    struct file *parent;
    char         path[512];
    char         name[512];
    int          flags;
    int          num_tabs;
    int          open_children;
    int          color_loc;
} file;

/* global vars */
static yed_plugin *Self;
static array_t     hidden_items;
static array_t     image_extensions;
static array_t     archive_extensions;
static array_t     files;
static time_t      last_time;
static time_t      wait_time;

/* internal functions*/
static void        _tree_view(int n_args, char **args);
static void        _tree_view_init(void);
static void        _tree_view_add_dir(int idx);
static void        _tree_view_remove_dir(int idx);
static void        _tree_view_select(void);
static void        _tree_view_line_handler(yed_event *event);
static void        _tree_view_key_pressed_handler(yed_event *event);
static void        _tree_view_update_handler(yed_event *event);
static void        _tree_view_unload(yed_plugin *self);

/* internal helper functions */
static yed_buffer *_get_or_make_buff(void);
static void        _add_hidden_items(void);
static void        _add_archive_extensions(void);
static void        _add_image_extensions(void);
static void        _clear_files(void);
static file       *_init_file(int parent_idx, char *path, char *name,
                              int if_dir, int num_tabs, int color_loc);

int yed_plugin_boot(yed_plugin *self) {
    yed_event_handler tree_view_key;
    yed_event_handler tree_view_line;
    yed_event_handler tree_view_update;

    YED_PLUG_VERSION_CHECK();

    Self = self;

    if (yed_get_var("tree-view-update-period") == NULL) {
        yed_set_var("tree-view-update-period", "5");
    }

    if (yed_get_var("tree-view-hidden-items") == NULL) {
        yed_set_var("tree-view-hidden-items", "");
    }

    if (yed_get_var("tree-view-image-extensions") == NULL) {
        yed_set_var("tree-view-image-extensions", "");
    }

    if (yed_get_var("tree-view-archive-extensions") == NULL) {
        yed_set_var("tree-view-archive-extensions", "");
    }

    if (yed_get_var("tree-view-child-char-l") == NULL) {
        yed_set_var("tree-view-child-char-l", "└");
    }

    if (yed_get_var("tree-view-child-char-i") == NULL) {
        yed_set_var("tree-view-child-char-i", "│");
    }

    if (yed_get_var("tree-view-child-char-t") == NULL) {
        yed_set_var("tree-view-child-char-t", "├");
    }

    yed_plugin_set_command(self, "tree-view", _tree_view);

    yed_plugin_set_unload_fn(self, _tree_view_unload);

    _tree_view_init();

    wait_time = atoi(yed_get_var("tree-view-update-period"));
    last_time = time(NULL);

    tree_view_key.kind = EVENT_KEY_PRESSED;
    tree_view_key.fn   = _tree_view_key_pressed_handler;
    yed_plugin_add_event_handler(self, tree_view_key);

    tree_view_line.kind = EVENT_LINE_PRE_DRAW;
    tree_view_line.fn   = _tree_view_line_handler;
    yed_plugin_add_event_handler(self, tree_view_line);

    tree_view_update.kind = EVENT_PRE_PUMP;
    tree_view_update.fn   = _tree_view_update_handler;
    yed_plugin_add_event_handler(self, tree_view_update);


    return 0;
}

static void _tree_view(int n_args, char **args) {
    if (array_len(files) == 0) {
        _tree_view_init();
    }

    YEXE("special-buffer-prepare-focus", "*tree-view-list");

    if (ys->active_frame) {
        YEXE("buffer", "*tree-view-list");
    }

    yed_set_cursor_far_within_frame(ys->active_frame, 1, 1);
}

static void _tree_view_init(void) {
    file       *dot;
    yed_buffer *buff;
    char       *tmp;

    if (array_len(files) == 0) {
        files = array_make(file *);

        _add_hidden_items();
        _add_archive_extensions();
        _add_image_extensions();
    } else {
        _clear_files();
    }

    buff = _get_or_make_buff();
    buff->flags &= ~BUFF_RD_ONLY;
    yed_buff_clear_no_undo(buff);
    buff->flags |= BUFF_RD_ONLY;

    dot = _init_file(IS_ROOT, ".", ",", IS_DIR, -1, 1);
    array_push(files, dot);

    _tree_view_add_dir(0);
}

static void _tree_view_add_dir(int idx) {
    char          **str_it;
    file           *f;
    file           *new_f;
    file           *prev_f;
    yed_buffer     *buff;
    struct dirent  *de;
    DIR            *dr;
    FILE           *fs;
    int             new_idx;
    int             dir;
    int             tabs;
    int             first;
    int             i;
    int             j;
    int             color_loc;
    char            path[512];
    char            name[512];
    char            write_name[512];
    char            new_name[512];
    struct stat     statbuf;

    buff = _get_or_make_buff();
    f    = *(file **)array_item(files, idx);
    dr   = opendir(f->path);

    if (dr == NULL) { return; }

    buff->flags &= ~BUFF_RD_ONLY;

    new_idx = idx+1;
    first   = 1;
    while ((de = readdir(dr)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        array_traverse(hidden_items, str_it) {
            if (strstr(de->d_name, (*str_it))) {
                goto cont;
            }
        }

        if (new_idx > 1) {
            yed_buff_insert_line_no_undo(buff, new_idx);
        }

        f->open_children = 1;

        memset(path, sizeof(char[512]), '0');
        sprintf(path, "%s/%s", f->path, de->d_name);

        tabs = f->num_tabs+1;
        memset(name, sizeof(char[512]), '0');
        memset(write_name, sizeof(char[512]), '0');
        color_loc = tabs * yed_get_tab_width();
        if (tabs > 0) {
            color_loc += 1;
            for  (i = 0; i < tabs; i++) {
                strcat(write_name, yed_get_var("tree-view-child-char-i"));
                for (j = 0; j < yed_get_tab_width()-1; j++) {
                    strcat(write_name, " ");
                }
            }
            strcat(write_name, yed_get_var("tree-view-child-char-l"));
        }
        strcat(write_name, de->d_name);
        strcat(name, de->d_name);

        if (!first && tabs > 0) {
            prev_f = *(file **)array_item(files, new_idx-1);

            memset(new_name, sizeof(char[512]), '0');
            if (tabs > 0) {
                for  (i = 0; i < tabs; i++) {
                    strcat(new_name, yed_get_var("tree-view-child-char-i"));
                    for (j = 0; j < yed_get_tab_width()-1; j++) {
                        strcat(new_name, " ");
                    }
                }
                strcat(new_name, yed_get_var("tree-view-child-char-t"));
            }
            strcat(new_name, prev_f->name);
            yed_line_clear(buff, new_idx-1);
            yed_buff_insert_string_no_undo(buff, new_name, new_idx-1, 1);
        }
skip:;

        yed_buff_insert_string_no_undo(buff, write_name, new_idx, 1);

        dir = 0;
        if (lstat(path, &statbuf) == 0) {
            switch (statbuf.st_mode & S_IFMT) {
                case S_IFDIR:
                    dir = IS_DIR;
                    break;
                case S_IFLNK:
                    fs = fopen(path, "r");
                    if (!fs && errno == 2) {
                        dir = IS_B_LINK;
                    } else {
                        dir = IS_LINK;
                    }
                    break;
                case S_IFBLK:
                case S_IFCHR:
                    dir = IS_DEVICE;
                    break;
                default:
                    if (statbuf.st_mode & S_IXUSR) {
                        dir = IS_EXEC;
                    } else {
                        array_traverse(archive_extensions, str_it) {
                            if (strstr(de->d_name, (*str_it))) {
                                dir = IS_ARCHIVE;
                                goto break_switch;
                            }
                        }

                        array_traverse(image_extensions, str_it) {
                            if (strstr(de->d_name, (*str_it))) {
                                dir = IS_IMAGE;
                                goto break_switch;
                            }
                        }

                        dir = IS_FILE;
                    }
                    break;
            }
        }
break_switch:;

        new_f = _init_file(idx, path, name, dir, tabs, color_loc);

        if (new_idx >= array_len(files)) {
            array_push(files, new_f);
        } else {
            array_insert(files, new_idx, new_f);
        }
        new_idx++;
        first = 0;

cont:;
    }

    closedir(dr);

    buff->flags |= BUFF_RD_ONLY;
}

static void _tree_view_remove_dir(int idx) {
    yed_buffer *buff;
    file       *f;
    file       *remove;
    int         next_idx;

    next_idx = idx + 1;

    buff = _get_or_make_buff();
    buff->flags &= ~BUFF_RD_ONLY;

    f = *(file **)array_item(files, idx);

    remove = *(file **)array_item(files, next_idx);
    while (remove->num_tabs > f->num_tabs) {
        free(remove);
        array_delete(files, next_idx);
        yed_buff_delete_line(buff, next_idx);

        remove = *(file **)array_item(files, next_idx);
    }

    f->open_children = 0;

    buff->flags |= BUFF_RD_ONLY;
}

static void _tree_view_select(void) {
    file *f;

    f = *(file **)array_item(files, ys->active_frame->cursor_line);

    if (f->flags == IS_DIR) {
        if (f->open_children) {
            _tree_view_remove_dir(ys->active_frame->cursor_line);
        } else {
            _tree_view_add_dir(ys->active_frame->cursor_line);
        }
    } else {
        YEXE("special-buffer-prepare-jump-focus", f->path);
        YEXE("buffer", f->path);
    }
}

static void _tree_view_line_handler(yed_event *event) {
    yed_frame  *frame;
    yed_attrs  *ait;
    file       *f;
    yed_attrs  *attr_tmp;
    yed_attrs   attr_dir;
    yed_attrs   attr_exec;
    yed_attrs   attr_symb_link;
    yed_attrs   attr_device;
    yed_attrs   attr_graphic_img;
    yed_attrs   attr_archive;
    yed_attrs   attr_broken_link;
    yed_attrs   attr_file;
    yed_attrs   attr_lines;
    struct stat statbuf;
    char        buf[1024];
    int         loc;
    int         tc;
    int         len;

    if (event->frame         == NULL
    ||  event->frame->buffer == NULL
    ||  event->frame->buffer != _get_or_make_buff()) {
        return;
    }

    frame = event->frame;

    if (array_len(files) < event->row) { return; }

    f = *(file **) array_item(files, event->row);

    if (f->path == NULL) { return; }

    attr_dir         = ZERO_ATTR;
    attr_exec        = ZERO_ATTR;
    attr_symb_link   = ZERO_ATTR;
    attr_device      = ZERO_ATTR;
    attr_graphic_img = ZERO_ATTR;
    attr_archive     = ZERO_ATTR;
    attr_broken_link = ZERO_ATTR;
    attr_file        = ZERO_ATTR;
    attr_lines       = ZERO_ATTR;

    tc = !!yed_get_var("truecolor");

    attr_dir = yed_active_style_get_code_string();
    if (attr_dir.flags & ATTR_16) {
        attr_dir.flags = ATTR_16_LIGHT_FG;
        attr_dir.fg    = ATTR_16_BLUE;
    } else if (attr_dir.flags & ATTR_256) {
        attr_dir.fg    = MAYBE_CONVERT(RGB_32_hex(0080FF));
    } else if (attr_dir.flags & ATTR_RGB) {
        attr_dir.fg    = MAYBE_CONVERT(RGB_32_hex(0080FF));
    }


    attr_exec = yed_active_style_get_code_string();
    if (attr_exec.flags & ATTR_16) {
        attr_exec.flags = ATTR_16_LIGHT_FG;
        attr_exec.fg    = ATTR_16_GREEN;
    } else if (attr_exec.flags & ATTR_256) {
        attr_exec.fg    = MAYBE_CONVERT(RGB_32_hex(00CC00));
    } else if (attr_exec.flags & ATTR_RGB) {
        attr_exec.fg    = MAYBE_CONVERT(RGB_32_hex(00CC00));
    }

    attr_symb_link = yed_active_style_get_code_string();
    if (attr_symb_link.flags & ATTR_16) {
        attr_symb_link.flags = ATTR_16_LIGHT_FG;
        attr_symb_link.fg    = ATTR_16_GREEN;
    } else if (attr_symb_link.flags & ATTR_256) {
        attr_symb_link.fg    = MAYBE_CONVERT(RGB_32_hex(66FFFF));
    } else if (attr_symb_link.flags & ATTR_RGB) {
        attr_symb_link.fg    = MAYBE_CONVERT(RGB_32_hex(66FFFF));
    }

    attr_device = yed_active_style_get_code_string();
    if (attr_device.flags & ATTR_16) {
        attr_device.flags = ATTR_16_LIGHT_FG;
        attr_device.fg    = ATTR_16_YELLOW;
        attr_device.bg    = ATTR_16_BLACK;
    } else if (attr_device.flags & ATTR_256) {
        attr_device.fg    = MAYBE_CONVERT(RGB_32_hex(FFFF33));
        attr_device.bg    = MAYBE_CONVERT(RGB_32_hex(100000));
    } else if (attr_device.flags & ATTR_RGB) {
        attr_device.fg    = MAYBE_CONVERT(RGB_32_hex(FFFF33));
        attr_device.bg    = MAYBE_CONVERT(RGB_32_hex(100000));
    }

    attr_file = yed_active_style_get_code_string();
    if (attr_file.flags & ATTR_16) {
        attr_file.flags = ATTR_16_LIGHT_FG;
        attr_file.fg    = ATTR_16_GREY;
    } else if (attr_file.flags & ATTR_256) {
        attr_file.fg    = MAYBE_CONVERT(RGB_32_hex(FFFFFF));
    } else if (attr_file.flags & ATTR_RGB) {
        attr_file.fg    = MAYBE_CONVERT(RGB_32_hex(FFFFFF));
    }

    attr_lines = yed_active_style_get_code_string();
    if (attr_lines.flags & ATTR_16) {
        attr_lines.flags = ATTR_16_LIGHT_FG;
        attr_lines.fg    = ATTR_16_GREY;
    } else if (attr_lines.flags & ATTR_256) {
        attr_lines.fg    = MAYBE_CONVERT(RGB_32_hex(FFFFFF));
    } else if (attr_lines.flags & ATTR_RGB) {
        attr_lines.fg    = MAYBE_CONVERT(RGB_32_hex(FFFFFF));
    }

    attr_graphic_img = yed_active_style_get_code_string();
    if (attr_graphic_img.flags & ATTR_16) {
        attr_graphic_img.flags = ATTR_16_LIGHT_FG;
        attr_graphic_img.fg    = ATTR_16_MAGENTA;
    } else if (attr_graphic_img.flags & ATTR_256) {
        attr_graphic_img.fg    = MAYBE_CONVERT(RGB_32_hex(FF33FF));
    } else if (attr_graphic_img.flags & ATTR_RGB) {
        attr_graphic_img.fg    = MAYBE_CONVERT(RGB_32_hex(FF33FF));
    }

    attr_archive = yed_active_style_get_code_string();
    if (attr_archive.flags & ATTR_16) {
        attr_archive.flags = ATTR_16_LIGHT_FG;
        attr_archive.fg    = ATTR_16_RED;
    } else if (attr_archive.flags & ATTR_256) {
        attr_archive.fg    = MAYBE_CONVERT(RGB_32_hex(FF3333));
    } else if (attr_archive.flags & ATTR_RGB) {
        attr_archive.fg    = MAYBE_CONVERT(RGB_32_hex(FF3333));
    }

    attr_broken_link = yed_active_style_get_code_string();
    if (attr_broken_link.flags & ATTR_16) {
        attr_broken_link.flags = ATTR_16_LIGHT_FG;
        attr_broken_link.fg    = ATTR_16_RED;
        attr_broken_link.bg    = ATTR_16_BLACK;
    } else if (attr_broken_link.flags & ATTR_256) {
        attr_broken_link.fg    = MAYBE_CONVERT(RGB_32_hex(FF3333));
        attr_broken_link.bg    = MAYBE_CONVERT(RGB_32_hex(100000));
    } else if (attr_broken_link.flags & ATTR_RGB) {
        attr_broken_link.fg    = MAYBE_CONVERT(RGB_32_hex(FF3333));
        attr_broken_link.bg    = MAYBE_CONVERT(RGB_32_hex(100000));
    }

    switch (f->flags) {
        case IS_DIR:
            attr_tmp = &attr_dir;
            break;
        case IS_LINK:
            attr_tmp = &attr_symb_link;
            break;
        case IS_B_LINK:
            attr_tmp = &attr_broken_link;
            break;
        case IS_DEVICE:
            attr_tmp = &attr_device;
            break;
        case IS_ARCHIVE:
            attr_tmp = &attr_archive;
            break;
        case IS_IMAGE:
            attr_tmp = &attr_graphic_img;
            break;
        case IS_EXEC:
            attr_tmp = &attr_exec;
            break;
        case IS_FILE:
        default:
            attr_tmp = &attr_file;
            break;
    }

    loc = 1;
    array_traverse(event->line_attrs, ait) {
        if (loc > f->color_loc) {
            yed_combine_attrs(ait, attr_tmp);
        } else {
            yed_combine_attrs(ait, &attr_lines);
        }
        loc++;
    }
}

static void _tree_view_key_pressed_handler(yed_event *event) {
  yed_frame *eframe;

    eframe = ys->active_frame;

    if (event->key != ENTER
    ||  ys->interactive_command
    ||  !eframe
    ||  !eframe->buffer
    ||  strcmp(eframe->buffer->name, "*tree-view-list")) {
        return;
    }

    _tree_view_select();

    event->cancel = 1;
}

static void  _tree_view_update_handler(yed_event *event) {
    file    **f;
    char     *path;
    array_t   open_dirs;
    time_t    curr_time;
    int       idx;

    curr_time = time(NULL);

    if (curr_time > last_time + wait_time) {
        yed_log("update\n");

        open_dirs = array_make(char *);

        idx = 0;
        array_traverse(files, f) {
            if (idx == 0) { idx++; continue; }

            if ((*f)->open_children) {
                path = strdup((*f)->path);
                array_push(open_dirs, path);
            }

            idx++;
        }

        _tree_view_init();

        while (array_len(open_dirs) > 0) {
            path = *(char **)array_item(open_dirs, 0);

            idx = 0;
            array_traverse(files, f) {
                if (idx == 0) { idx++; continue; }

                if (strcmp((*f)->path, path) == 0) {
                    _tree_view_add_dir(idx);
                    array_delete(open_dirs, 0);
                    break;
                }

                idx++;
            }
        }

        last_time = curr_time;
    }
}

static yed_buffer *_get_or_make_buff(void) {
    yed_buffer *buff;

    buff = yed_get_buffer("*tree-view-list");

    if (buff == NULL) {
        buff = yed_create_buffer("*tree-view-list");
        buff->flags |= BUFF_RD_ONLY | BUFF_SPECIAL;
    }

    return buff;
}

static file *_init_file(int parent_idx, char *path, char *name, int if_dir, int num_tabs, int color_loc) {
    file *f;

    f = malloc(sizeof(file));
    memset(f, sizeof(file), '0');

    if (parent_idx == IS_ROOT) {
        f->parent = NULL;
    } else {
        f->parent = *(struct file **) array_item(files, parent_idx);
    }

    memset(f->path, sizeof(char[512]), '0');
    strcat(f->path, path);

    memset(f->name, sizeof(char[512]), '0');
    strcat(f->name, name);

    f->flags         = if_dir;
    f->num_tabs      = num_tabs;
    f->open_children = 0;
    f->color_loc     = color_loc;

    return f;
}

static void _clear_files(void) {
    file *f;
    while (array_len(files) > 0) {
        f = *(file **)array_item(files, 0);
        free(f);
        array_delete(files, 0);
    }
}

static void _add_hidden_items(void) {
    char       *token;
    char       *tmp;
    const char  s[2] = " ";

    if (array_len(hidden_items) > 0) {
        array_clear(hidden_items);
    }
    hidden_items = array_make(char *);
    token = strtok(yed_get_var("tree-view-hidden-items"), s);
    while(token != NULL) {
        tmp = strdup(token);
        array_push(hidden_items, tmp);
        token = strtok(NULL, s);
    }
}

static void _add_archive_extensions(void) {
    char       *token;
    char       *tmp;
    char       *str;
    const char  s[2] = " ";

    if (array_len(archive_extensions) > 0) {
        array_clear(archive_extensions);
    }
    archive_extensions = array_make(char *);

    str = strdup(".a");       array_push(archive_extensions, str);
    str = strdup(".ar");      array_push(archive_extensions, str);
    str = strdup(".cpio");    array_push(archive_extensions, str);
    str = strdup(".shar");    array_push(archive_extensions, str);
    str = strdup(".LBR");     array_push(archive_extensions, str);
    str = strdup(".iso");     array_push(archive_extensions, str);
    str = strdup(".lbr");     array_push(archive_extensions, str);
    str = strdup(".mar");     array_push(archive_extensions, str);
    str = strdup(".sbx");     array_push(archive_extensions, str);
    str = strdup(".tar");     array_push(archive_extensions, str);
    str = strdup(".br");      array_push(archive_extensions, str);
    str = strdup(".bz2");     array_push(archive_extensions, str);
    str = strdup(".F");       array_push(archive_extensions, str);
    str = strdup(".gz");      array_push(archive_extensions, str);
    str = strdup(".lz");      array_push(archive_extensions, str);
    str = strdup(".lz4");     array_push(archive_extensions, str);
    str = strdup(".lzma");    array_push(archive_extensions, str);
    str = strdup(".lzo");     array_push(archive_extensions, str);
    str = strdup(".rz");      array_push(archive_extensions, str);
    str = strdup(".sfark");   array_push(archive_extensions, str);
    str = strdup(".sz");      array_push(archive_extensions, str);
    str = strdup(".xz");      array_push(archive_extensions, str);
    str = strdup(".z");       array_push(archive_extensions, str);
    str = strdup(".Z");       array_push(archive_extensions, str);
    str = strdup(".zst");     array_push(archive_extensions, str);
    str = strdup(".7z");      array_push(archive_extensions, str);
    str = strdup(".s7z");     array_push(archive_extensions, str);
    str = strdup(".ace");     array_push(archive_extensions, str);
    str = strdup(".afa");     array_push(archive_extensions, str);
    str = strdup(".alz");     array_push(archive_extensions, str);
    str = strdup(".apk");     array_push(archive_extensions, str);
    str = strdup(".arc");     array_push(archive_extensions, str);
    str = strdup(".ark");     array_push(archive_extensions, str);
    str = strdup(".cdx");     array_push(archive_extensions, str);
    str = strdup(".arj");     array_push(archive_extensions, str);
    str = strdup(".b1");      array_push(archive_extensions, str);
    str = strdup(".b6z");     array_push(archive_extensions, str);
    str = strdup(".ba");      array_push(archive_extensions, str);
    str = strdup(".bh");      array_push(archive_extensions, str);
    str = strdup(".cab");     array_push(archive_extensions, str);
    str = strdup(".car");     array_push(archive_extensions, str);
    str = strdup(".cfs");     array_push(archive_extensions, str);
    str = strdup(".cpt");     array_push(archive_extensions, str);
    str = strdup(".dar");     array_push(archive_extensions, str);
    str = strdup(".dd");      array_push(archive_extensions, str);
    str = strdup(".dgc");     array_push(archive_extensions, str);
    str = strdup(".dmg");     array_push(archive_extensions, str);
    str = strdup(".ear");     array_push(archive_extensions, str);
    str = strdup(".gca");     array_push(archive_extensions, str);
    str = strdup(".genozip"); array_push(archive_extensions, str);
    str = strdup(".ha");      array_push(archive_extensions, str);
    str = strdup(".hki");     array_push(archive_extensions, str);
    str = strdup(".ice");     array_push(archive_extensions, str);
    str = strdup(".jar");     array_push(archive_extensions, str);
    str = strdup(".kgb");     array_push(archive_extensions, str);
    str = strdup(".lzh");     array_push(archive_extensions, str);
    str = strdup(".lha");     array_push(archive_extensions, str);
    str = strdup(".lzx");     array_push(archive_extensions, str);
    str = strdup(".pak");     array_push(archive_extensions, str);
    str = strdup(".partimg"); array_push(archive_extensions, str);
    str = strdup(".paq6");    array_push(archive_extensions, str);
    str = strdup(".paz7");    array_push(archive_extensions, str);
    str = strdup(".paq8");    array_push(archive_extensions, str);
    str = strdup(".pea");     array_push(archive_extensions, str);
    str = strdup(".phar");    array_push(archive_extensions, str);
    str = strdup(".pim");     array_push(archive_extensions, str);
    str = strdup(".pit");     array_push(archive_extensions, str);
    str = strdup(".qda");     array_push(archive_extensions, str);
    str = strdup(".rar");     array_push(archive_extensions, str);
    str = strdup(".rk");      array_push(archive_extensions, str);
    str = strdup(".sda");     array_push(archive_extensions, str);
    str = strdup(".sea");     array_push(archive_extensions, str);
    str = strdup(".sen");     array_push(archive_extensions, str);
    str = strdup(".sfx");     array_push(archive_extensions, str);
    str = strdup(".shk");     array_push(archive_extensions, str);
    str = strdup(".sit");     array_push(archive_extensions, str);
    str = strdup(".sitx");    array_push(archive_extensions, str);
    str = strdup(".sqx");     array_push(archive_extensions, str);
    str = strdup(".tar.gz");  array_push(archive_extensions, str);
    str = strdup(".tgz");     array_push(archive_extensions, str);
    str = strdup(".tar.Z");   array_push(archive_extensions, str);
    str = strdup(".tar.bz2"); array_push(archive_extensions, str);
    str = strdup(".tbz2");    array_push(archive_extensions, str);
    str = strdup(".tar.lz");  array_push(archive_extensions, str);
    str = strdup(".tlz");     array_push(archive_extensions, str);
    str = strdup(".tar.xz");  array_push(archive_extensions, str);
    str = strdup(".txz");     array_push(archive_extensions, str);
    str = strdup(".tar.zst"); array_push(archive_extensions, str);
    str = strdup(".uc");      array_push(archive_extensions, str);
    str = strdup(".uc0");     array_push(archive_extensions, str);
    str = strdup(".uc2");     array_push(archive_extensions, str);
    str = strdup(".ucn");     array_push(archive_extensions, str);
    str = strdup(".ur2");     array_push(archive_extensions, str);
    str = strdup(".ue2");     array_push(archive_extensions, str);
    str = strdup(".uca");     array_push(archive_extensions, str);
    str = strdup(".uha");     array_push(archive_extensions, str);
    str = strdup(".war");     array_push(archive_extensions, str);
    str = strdup(".wim");     array_push(archive_extensions, str);
    str = strdup(".xar");     array_push(archive_extensions, str);
    str = strdup(".xp3");     array_push(archive_extensions, str);
    str = strdup(".yz1");     array_push(archive_extensions, str);
    str = strdup(".zip");     array_push(archive_extensions, str);
    str = strdup(".zipx");    array_push(archive_extensions, str);
    str = strdup(".zoo");     array_push(archive_extensions, str);
    str = strdup(".zpaq");    array_push(archive_extensions, str);
    str = strdup(".zz");      array_push(archive_extensions, str);
    str = strdup(".deb");     array_push(archive_extensions, str);
    str = strdup(".pkg");     array_push(archive_extensions, str);
    str = strdup(".mpkg");    array_push(archive_extensions, str);
    str = strdup(".rpm");     array_push(archive_extensions, str);
    str = strdup(".msi");     array_push(archive_extensions, str);
    str = strdup(".crx");     array_push(archive_extensions, str);

    token = strtok(yed_get_var("tree-view-archive-extensions"), s);
    while(token != NULL) {
        tmp = strdup(token);
        array_push(archive_extensions, tmp);
        token = strtok(NULL, s);
    }
}

static void _add_image_extensions(void) {
    char       *token;
    char       *tmp;
    char       *str;
    const char  s[2] = " ";

    if (array_len(image_extensions) > 0) {
        array_clear(image_extensions);
    }
    image_extensions = array_make(char *);

    str = strdup(".jpg");  array_push(image_extensions, str);
    str = strdup(".jpeg"); array_push(image_extensions, str);
    str = strdup(".jpe");  array_push(image_extensions, str);
    str = strdup(".jif");  array_push(image_extensions, str);
    str = strdup(".jfif"); array_push(image_extensions, str);
    str = strdup(".jfi");  array_push(image_extensions, str);
    str = strdup(".png");  array_push(image_extensions, str);
    str = strdup(".gif");  array_push(image_extensions, str);
    str = strdup(".webp"); array_push(image_extensions, str);
    str = strdup(".tiff"); array_push(image_extensions, str);
    str = strdup(".tif");  array_push(image_extensions, str);
    str = strdup(".psd");  array_push(image_extensions, str);
    str = strdup(".raw");  array_push(image_extensions, str);
    str = strdup(".arw");  array_push(image_extensions, str);
    str = strdup(".cr2");  array_push(image_extensions, str);
    str = strdup(".nrw");  array_push(image_extensions, str);
    str = strdup(".k25");  array_push(image_extensions, str);
    str = strdup(".bmp");  array_push(image_extensions, str);
    str = strdup(".dib");  array_push(image_extensions, str);
    str = strdup(".heif"); array_push(image_extensions, str);
    str = strdup(".heic"); array_push(image_extensions, str);
    str = strdup(".ind");  array_push(image_extensions, str);
    str = strdup(".indd"); array_push(image_extensions, str);
    str = strdup(".indt"); array_push(image_extensions, str);
    str = strdup(".jp2");  array_push(image_extensions, str);
    str = strdup(".j2k");  array_push(image_extensions, str);
    str = strdup(".jpf");  array_push(image_extensions, str);
    str = strdup(".jpx");  array_push(image_extensions, str);
    str = strdup(".jpm");  array_push(image_extensions, str);
    str = strdup(".mj2");  array_push(image_extensions, str);
    str = strdup(".svg");  array_push(image_extensions, str);
    str = strdup(".svgz"); array_push(image_extensions, str);
    str = strdup(".ai");   array_push(image_extensions, str);
    str = strdup(".eps");  array_push(image_extensions, str);
    str = strdup(".pdf");  array_push(image_extensions, str);

    token = strtok(yed_get_var("tree-view-image-extensions"), s);
    while(token != NULL) {
        tmp = strdup(token);
        array_push(image_extensions, tmp);
        token = strtok(NULL, s);
    }
}

static void _tree_view_unload(yed_plugin *self) {
    _clear_files();
}
