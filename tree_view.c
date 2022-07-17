#include <yed/plugin.h>

#define IS_FILE  0
#define IS_DIR   1
#define IS_ROOT -1

/* global structs */
typedef struct {
    struct file *parent;
    char         path[512];
    char         name[512];
    int          if_dir;
    int          num_tabs;
    int          open_children;
    int          color_loc;
} file;

/* global vars */
static yed_plugin *Self;
static array_t     hidden_items;
static array_t     files;

/* internal functions*/
static void        _tree_view(int n_args, char **args);
static void        _tree_view_init(void);
static void        _tree_view_add_dir(int idx);
static void        _tree_view_remove_dir(int idx);
static void        _tree_view_select(void);
static void        _tree_view_line_handler(yed_event *event);
static void        _tree_view_key_pressed_handler(yed_event *event);
static void        _tree_view_unload(yed_plugin *self);

/* internal helper functions */
static yed_buffer *_get_or_make_buff(void);
static void        _clear_files(void);
static file       *_init_file(int parent_idx, char *path, char *name,
                              int if_dir, int num_tabs, int color_loc);

int yed_plugin_boot(yed_plugin *self) {
    yed_event_handler tree_view_key;
    yed_event_handler tree_view_line;

    YED_PLUG_VERSION_CHECK();

    tree_view_key.kind = EVENT_KEY_PRESSED;
    tree_view_key.fn   = _tree_view_key_pressed_handler;
    yed_plugin_add_event_handler(self, tree_view_key);

    tree_view_line.kind = EVENT_LINE_PRE_DRAW;
    tree_view_line.fn   = _tree_view_line_handler;
    yed_plugin_add_event_handler(self, tree_view_line);

    Self = self;

    if (yed_get_var("tree-view-hidden-items") == NULL) {
        yed_set_var("tree-view-hidden-items", "");
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

    return 0;
}

static void _tree_view(int n_args, char **args) {
    char       *token;
    char       *tmp;
    const char  s[2] = " ";

    if (array_len(files) == 0) {
        files = array_make(file *);

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
/*             if (strcmp(de->d_name, (*str_it)) == 0) { */
/*                 goto cont; */
/*             } */
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
            if (S_ISDIR(statbuf.st_mode)) {
                dir = 1;
            }
        }

        if (dir) {
            new_f = _init_file(idx, path, name, IS_DIR, tabs, color_loc);
        } else {
            new_f = _init_file(idx, path, name, IS_FILE, tabs, color_loc);
        }

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
    file     *f;

    f = *(file **)array_item(files, ys->active_frame->cursor_line);

    if (f->if_dir == IS_DIR) {
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
    yed_frame *frame;
    yed_attrs *ait;
    file      *f;
    yed_attrs *attr_tmp;
    yed_attrs  attr_dir;
    yed_attrs  attr_exec;
    yed_attrs  attr_symb_link;
    yed_attrs  attr_device;
    yed_attrs  attr_graphic_img;
    yed_attrs  attr_archive;
    yed_attrs  attr_broken_link;
    yed_attrs  attr_file;
    int        loc;

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

/*     attr_dir = yed_active_style_get_code_string(); */
    attr_dir.flags      = ATTR_16 | ATTR_16_LIGHT_FG;
    attr_dir.fg         = ATTR_16_BLUE;

/*     attr_exec = yed_active_style_get_code_string(); */
    attr_exec.flags      = ATTR_16 | ATTR_16_LIGHT_FG;
    attr_exec.fg         = ATTR_16_GREEN;

/*     attr_symb_link = yed_active_style_get_code_string(); */
    attr_symb_link.flags      = ATTR_16 | ATTR_16_LIGHT_FG;
    attr_symb_link.fg         = ATTR_16_CYAN;

/*     attr_device = yed_active_style_get_code_string(); */
    attr_device.flags      = ATTR_16 | ATTR_16_LIGHT_FG;
    attr_device.fg         = ATTR_16_YELLOW;
    attr_device.bg         = ATTR_16_BLACK;

    attr_graphic_img = ZERO_ATTR;
    attr_archive     = ZERO_ATTR;
    attr_broken_link = ZERO_ATTR;

/*     attr_file = yed_active_style_get_code_string(); */
    attr_file.flags      = ATTR_16 | ATTR_16_LIGHT_FG;
    attr_file.fg         = ATTR_16_GREY;

    struct stat statbuf;
    if (lstat(f->path, &statbuf) != 0) { return; }

    switch (statbuf.st_mode & S_IFMT) {
        case S_IFDIR:
            attr_tmp = &attr_dir;
            break;
        case S_IFLNK:
            attr_tmp = &attr_symb_link;
            break;
        case S_IFBLK:
        case S_IFCHR:
            attr_tmp = &attr_device;
            break;
        default:
            if (statbuf.st_mode & S_IXUSR) {
                attr_tmp = &attr_exec;
            } else {
                attr_tmp = &attr_file;
            }
            break;
    }

    loc = 1;
    array_traverse(event->line_attrs, ait) {
        if (loc > f->color_loc) {
            yed_combine_attrs(ait, attr_tmp);
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

    f->if_dir        = if_dir;
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

static void _tree_view_unload(yed_plugin *self) {
    _clear_files();
}
