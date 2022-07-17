#include <yed/plugin.h>

/* global vars */
static yed_plugin *Self;
static array_t     hidden_items;
static array_t     paths;

/* internal functions*/
static void        _tree_view(int n_args, char **args);
static void        _tree_view_init(void);
static void        _tree_view_add_dir(char *dir, int loc);
static void        _tree_view_select(void);
static void        _tree_view_line_handler(yed_event *event);
static yed_buffer *_get_or_make_buff(void);
static int         _count_tabs(int loc);
static void        _tree_view_key_pressed_handler(yed_event *event);
static void        _tree_view_unload(yed_plugin *self);

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

    if (yed_get_var("tree-view-child-char") == NULL) {
        yed_set_var("tree-view-child-char", " ");
    }

    yed_plugin_set_command(self, "tree-view", _tree_view);

    yed_plugin_set_unload_fn(self, _tree_view_unload);

    return 0;
}

static void _tree_view(int n_args, char **args) {
    char       *token;
    char       *tmp;
    const char  s[2] = " ";

    if (array_len(paths) > 0) {
        array_clear(paths);
    }
    paths = array_make(char *);

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

    YEXE("special-buffer-prepare-focus", "*tree-view-list");

    if (ys->active_frame) {
        YEXE("buffer", "*tree-view-list");
    }

    yed_set_cursor_far_within_frame(ys->active_frame, 1, 1);
}

static void _tree_view_init(void) {
    yed_buffer *buff;
    char       *tmp;

    buff = _get_or_make_buff();
    buff->flags &= ~BUFF_RD_ONLY;
    yed_buff_clear_no_undo(buff);
    buff->flags |= BUFF_RD_ONLY;


    tmp = strdup(".");
    array_push(paths, tmp);

    _tree_view_add_dir(".", 0);

    _tree_view_add_dir("./test_dir", 7);
/*     _tree_view_add_dir("/dev", 0); */
}

static void _tree_view_add_dir(char *dir, int loc) {
    char          **str_it;
    char           *tmp_str;
    char           *dup_str;
    char           *child_char;
    yed_buffer     *buff;
    struct dirent  *de;
    DIR            *dr;
    char            path[512];
    char            out_path[512];
    int             row;
    int             i;
    int             new_loc;
    int             tabs;

    buff = _get_or_make_buff();

    dr = opendir(dir);

    if (dr == NULL) { return; }

    buff->flags &= ~BUFF_RD_ONLY;

    row = loc+1;
    new_loc = loc;
    while ((de = readdir(dr)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        array_traverse(hidden_items, str_it) {
            if (strcmp(de->d_name, (*str_it)) == 0) {
                goto cont;
            }
        }

        if (row > 1) {
            yed_buff_insert_line_no_undo(buff, row);
        }

        memset(path, sizeof(char[512]), '0');
        tmp_str = *(char**) array_item(paths, loc);
        sprintf(path, "%s/%s", tmp_str, de->d_name);
        dup_str = strdup(path);
        array_insert(paths, ++new_loc, dup_str);

        memset(out_path, sizeof(char[512]), '0');
        tabs = _count_tabs(new_loc) * yed_get_tab_width();
        if (tabs == 0) {
            tabs = 1;
        }

        if (tabs > 1) {
            child_char = yed_get_var("tree-view-child-char");
            strcat(out_path, strdup(child_char));
        }

        strcat(out_path, de->d_name);

        yed_buff_insert_string_no_undo(buff, out_path, row, tabs);

        row += 1;
cont:;
    }

    closedir(dr);

    buff->flags |= BUFF_RD_ONLY;
}

static void _tree_view_select(void) {

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

static int _count_tabs(int loc) {
    char       *str;
    int         ret = 0;

    str  = *(char **) array_item(paths, loc);

    for (int i = 0; i < strlen(str); i++) {
        if (str[i] == '/') {
            ret++;
        }
    }

    return ret-1;
}

static void _tree_view_line_handler(yed_event *event) {
    yed_frame *frame;
    yed_attrs *ait;
    char      *s;
    yed_attrs *attr_tmp;
    yed_attrs  attr_dir;
    yed_attrs  attr_exec;
    yed_attrs  attr_symb_link;
    yed_attrs  attr_device;
    yed_attrs  attr_graphic_img;
    yed_attrs  attr_archive;
    yed_attrs  attr_broken_link;
    yed_attrs  attr_file;

    if (event->frame         == NULL
    ||  event->frame->buffer == NULL
    ||  event->frame->buffer != _get_or_make_buff()) {
        return;
    }

    frame = event->frame;

    if (array_len(paths) < event->row) { return; }

    s = *(char **) array_item(paths, event->row);

    if (s == NULL) { return; }

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
    if (lstat(s, &statbuf) != 0) { return; }

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
    array_traverse(event->line_attrs, ait) {
        yed_combine_attrs(ait, attr_tmp);
    }
}

static void _tree_view_key_pressed_handler(yed_event *event) {

}

static void _tree_view_unload(yed_plugin *self) {
}
