/*
 * MIT License
 * Copyright (c) 2026 Felipe da Silva Braz
 *
 * editor.c - Editor de texto simples com interface grafica (GTK3)
 *
 * Funcionalidades:
 *   - Abrir arquivo de texto (dialogo de selecao de arquivo)
 *   - Editar o texto livremente em uma area de edicao (GtkTextView)
 *   - Salvar / Salvar como
 *   - Novo documento
 *   - Aviso ao fechar/abrir/novo se houver alteracoes nao salvas
 *   - Titulo da janela mostra o nome do arquivo e um indicador de modificado (*)
 *   - Barra de status com mensagens rapidas
 *   - Atalhos de teclado: Ctrl+N novo, Ctrl+O abrir, Ctrl+S salvar,
 *                          Ctrl+Shift+S salvar como, Ctrl+Q sair
 *
 * Dependencias: GTK 3 (libgtk-3-dev)
 *
 * Compilar:
 *   gcc -Wall -Wextra -o editor editor.c $(pkg-config --cflags --libs gtk+-3.0)
 *
 * Executar:
 *   ./editor_gtk
 */

#include <gtk/gtk.h>
#include <string.h>

/* ---------- Estado global da aplicacao ---------- */

typedef struct {
    GtkWidget *window;
    GtkWidget *text_view;
    GtkTextBuffer *buffer;
    GtkWidget *statusbar;
    guint statusbar_ctx;

    char *filename;   /* NULL se ainda nao houver arquivo associado */
    gboolean modified;
} AppState;

/* ---------- Funcoes auxiliares ---------- */

static void set_status(AppState *app, const char *msg) {
    gtk_statusbar_pop(GTK_STATUSBAR(app->statusbar), app->statusbar_ctx);
    gtk_statusbar_push(GTK_STATUSBAR(app->statusbar), app->statusbar_ctx, msg);
}

static void update_title(AppState *app) {
    const char *base = app->filename ? app->filename : "Sem titulo";
    char title[600];
    snprintf(title, sizeof(title), "%s%s - Editor de Texto Simples",
             base, app->modified ? " *" : "");
    gtk_window_set_title(GTK_WINDOW(app->window), title);
}

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data) {
    (void)buffer;
    AppState *app = (AppState *)user_data;
    if (!app->modified) {
        app->modified = TRUE;
        update_title(app);
    }
}

/* Pergunta ao usuario se deseja descartar alteracoes nao salvas.
 * Retorna TRUE se pode prosseguir (usuario confirmou ou nao havia alteracoes). */
static gboolean confirm_discard_changes(AppState *app) {
    if (!app->modified) return TRUE;

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_NONE,
        "Ha alteracoes nao salvas. Deseja continuar mesmo assim e perde-las?");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog),
                            "_Cancelar", GTK_RESPONSE_CANCEL,
                            "_Descartar", GTK_RESPONSE_OK,
                            NULL);
    gint resp = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return resp == GTK_RESPONSE_OK;
}

static void show_error(AppState *app, const char *msg) {
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK,
        "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* ---------- Operacoes de arquivo ---------- */

static gboolean load_file(AppState *app, const char *path) {
    gchar *conteudo = NULL;
    gsize length = 0;
    GError *error = NULL;

    if (!g_file_get_contents(path, &conteudo, &length, &error)) {
        char msg[700];
        snprintf(msg, sizeof(msg), "Nao foi possivel abrir o arquivo:\n%s",
                 error ? error->message : "erro desconhecido");
        show_error(app, msg);
        if (error) g_error_free(error);
        return FALSE;
    }

    /* Bloqueia temporariamente o sinal de "changed" para nao marcar como
     * modificado ao carregar o conteudo inicial. */
    g_signal_handlers_block_by_func(app->buffer, G_CALLBACK(on_buffer_changed), app);
    gtk_text_buffer_set_text(app->buffer, conteudo, (gint)length);
    g_signal_handlers_unblock_by_func(app->buffer, G_CALLBACK(on_buffer_changed), app);

    g_free(conteudo);

    g_free(app->filename);
    app->filename = g_strdup(path);
    app->modified = FALSE;
    update_title(app);

    char status[600];
    snprintf(status, sizeof(status), "Arquivo aberto: %s", path);
    set_status(app, status);
    return TRUE;
}

static gboolean save_file(AppState *app, const char *path) {
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(app->buffer, &start);
    gtk_text_buffer_get_end_iter(app->buffer, &end);
    gchar *texto = gtk_text_buffer_get_text(app->buffer, &start, &end, FALSE);

    GError *error = NULL;
    gboolean ok = g_file_set_contents(path, texto, -1, &error);
    g_free(texto);

    if (!ok) {
        char msg[700];
        snprintf(msg, sizeof(msg), "Nao foi possivel salvar o arquivo:\n%s",
                 error ? error->message : "erro desconhecido");
        show_error(app, msg);
        if (error) g_error_free(error);
        return FALSE;
    }

    g_free(app->filename);
    app->filename = g_strdup(path);
    app->modified = FALSE;
    update_title(app);

    char status[600];
    snprintf(status, sizeof(status), "Arquivo salvo: %s", path);
    set_status(app, status);
    return TRUE;
}

/* ---------- Dialogos ---------- */

static char *ask_open_path(AppState *app) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Abrir arquivo", GTK_WINDOW(app->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancelar", GTK_RESPONSE_CANCEL,
        "_Abrir", GTK_RESPONSE_ACCEPT,
        NULL);

    char *resultado = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        resultado = gtk_file_chooser_get_filename(chooser);
    }
    gtk_widget_destroy(dialog);
    return resultado; /* precisa g_free() pelo chamador */
}

static char *ask_save_path(AppState *app) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Salvar como", GTK_WINDOW(app->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancelar", GTK_RESPONSE_CANCEL,
        "_Salvar", GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    if (app->filename) {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), app->filename);
    } else {
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "sem_titulo.txt");
    }

    char *resultado = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        resultado = gtk_file_chooser_get_filename(chooser);
    }
    gtk_widget_destroy(dialog);
    return resultado; /* precisa g_free() pelo chamador */
}

/* ---------- Acoes de menu / atalhos ---------- */

static void action_new(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    AppState *app = (AppState *)user_data;
    if (!confirm_discard_changes(app)) return;

    g_signal_handlers_block_by_func(app->buffer, G_CALLBACK(on_buffer_changed), app);
    gtk_text_buffer_set_text(app->buffer, "", -1);
    g_signal_handlers_unblock_by_func(app->buffer, G_CALLBACK(on_buffer_changed), app);

    g_free(app->filename);
    app->filename = NULL;
    app->modified = FALSE;
    update_title(app);
    set_status(app, "Novo documento");
}

static void action_open(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    AppState *app = (AppState *)user_data;
    if (!confirm_discard_changes(app)) return;

    char *path = ask_open_path(app);
    if (path) {
        load_file(app, path);
        g_free(path);
    }
}

static void action_save(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    AppState *app = (AppState *)user_data;
    if (app->filename) {
        save_file(app, app->filename);
    } else {
        char *path = ask_save_path(app);
        if (path) {
            save_file(app, path);
            g_free(path);
        }
    }
}

static void action_save_as(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    AppState *app = (AppState *)user_data;
    char *path = ask_save_path(app);
    if (path) {
        save_file(app, path);
        g_free(path);
    }
}

static void action_quit(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    AppState *app = (AppState *)user_data;
    if (!confirm_discard_changes(app)) return;
    gtk_widget_destroy(app->window);
}

static void action_about(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    AppState *app = (AppState *)user_data;

    const gchar *autores[] = {"Felipe da Silva Braz", NULL};

    GtkWidget *dialog = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "Editor de Texto Simples");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "1.0");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog),
        "Um editor de texto simples e leve, escrito em C com GTK3.\n\n"
        "Permite abrir, editar e salvar arquivos de texto (.txt e similares) "
        "atraves de uma interface grafica com toolbar, area de edicao e "
        "atalhos de teclado.");
    gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dialog), "https://felipe.sb.nom.br");
    gtk_about_dialog_set_website_label(GTK_ABOUT_DIALOG(dialog), "Visite o nosso site");
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), autores);
    gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(dialog), GTK_LICENSE_MIT_X11);
    gtk_about_dialog_set_logo_icon_name(GTK_ABOUT_DIALOG(dialog), "accessories-text-editor");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* Trata os atalhos de teclado globais da janela, ja que eles nao estao
 * mais associados a itens de um menu "Arquivo". */
static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    AppState *app = (AppState *)user_data;
    guint state = event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK);

    if (state == GDK_CONTROL_MASK) {
        switch (event->keyval) {
        case GDK_KEY_n: case GDK_KEY_N:
            action_new(NULL, app);
            return TRUE;
        case GDK_KEY_o: case GDK_KEY_O:
            action_open(NULL, app);
            return TRUE;
        case GDK_KEY_s: case GDK_KEY_S:
            action_save(NULL, app);
            return TRUE;
        case GDK_KEY_q: case GDK_KEY_Q:
            action_quit(NULL, app);
            return TRUE;
        default:
            break;
        }
    } else if (state == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
        switch (event->keyval) {
        case GDK_KEY_s: case GDK_KEY_S:
            action_save_as(NULL, app);
            return TRUE;
        default:
            break;
        }
    }
    return FALSE;
}

/* Interceptar o fechamento pelo "x" da janela */
static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    (void)widget;
    (void)event;
    AppState *app = (AppState *)user_data;
    return !confirm_discard_changes(app); /* TRUE = impede o fechamento */
}

/* ---------- Montagem da interface ---------- */

/* Cria um botao de toolbar com icone e legenda (texto) sempre visivel,
 * independente do tema/estilo do sistema. */
static GtkToolItem *make_tool_button(const char *label, const char *icon_name,
                                      GCallback callback, gpointer user_data) {
    GtkToolItem *btn = gtk_tool_button_new(NULL, label);
    gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(btn), icon_name);
    /* Forca a legenda a aparecer mesmo em toolbars "somente icone" */
    gtk_tool_item_set_is_important(btn, TRUE);
    g_signal_connect(btn, "clicked", callback, user_data);
    return btn;
}

static GtkWidget *build_toolbar(AppState *app) {
    GtkWidget *toolbar = gtk_toolbar_new();
    /* Icone em cima, legenda embaixo de cada botao */
    gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_BOTH);

    GtkToolItem *btn_new = make_tool_button("Novo", "document-new",
                                             G_CALLBACK(action_new), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), btn_new, -1);

    GtkToolItem *btn_open = make_tool_button("Abrir", "document-open",
                                              G_CALLBACK(action_open), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), btn_open, -1);

    GtkToolItem *btn_save = make_tool_button("Salvar", "document-save",
                                              G_CALLBACK(action_save), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), btn_save, -1);

    GtkToolItem *btn_save_as = make_tool_button("Salvar como", "document-save-as",
                                                 G_CALLBACK(action_save_as), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), btn_save_as, -1);

    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_separator_tool_item_new(), -1);

    GtkToolItem *btn_about = make_tool_button("Sobre", "help-about",
                                               G_CALLBACK(action_about), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), btn_about, -1);

    GtkToolItem *btn_quit = make_tool_button("Sair", "application-exit",
                                              G_CALLBACK(action_quit), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), btn_quit, -1);

    return toolbar;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    AppState app;
    app.filename = NULL;
    app.modified = FALSE;

    /* Janela principal */
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(app.window), 800, 600);
    gtk_container_set_border_width(GTK_CONTAINER(app.window), 0);

    /* Layout vertical: toolbar, area de texto, statusbar */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app.window), vbox);

    GtkWidget *toolbar = build_toolbar(&app);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    /* Area de texto com rolagem */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    app.text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app.text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(app.text_view), TRUE);
    gtk_container_add(GTK_CONTAINER(scrolled), app.text_view);

    app.buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app.text_view));
    g_signal_connect(app.buffer, "changed", G_CALLBACK(on_buffer_changed), &app);

    /* Barra de status */
    app.statusbar = gtk_statusbar_new();
    app.statusbar_ctx = gtk_statusbar_get_context_id(GTK_STATUSBAR(app.statusbar), "geral");
    gtk_box_pack_start(GTK_BOX(vbox), app.statusbar, FALSE, FALSE, 0);
    set_status(&app, "Pronto");

    g_signal_connect(app.window, "delete-event", G_CALLBACK(on_delete_event), &app);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(on_key_press), &app);

    update_title(&app);

    /* Se um arquivo foi passado por linha de comando, abre automaticamente */
    if (argc > 1) {
        load_file(&app, argv[1]);
    }

    gtk_widget_show_all(app.window);
    gtk_main();

    g_free(app.filename);
    return 0;
}
