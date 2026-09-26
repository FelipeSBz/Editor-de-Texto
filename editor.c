/*
 * MIT License
 * Copyright (c) 2026 Felipe da Silva Braz
 *
 * editor.c - Editor de texto simples com interface grafica (GTK3)
 *
 * Funcionalidades:
 *   - Abrir arquivo de texto (dialogo de selecao de arquivo)
 *   - Editar o texto livremente em uma area de edição (GtkTextView)
 *   - Salvar / Salvar como
 *   - Novo documento
 *   - Aviso ao fechar/abrir/novo se houver alteracoes nao salvas
 *   - Titulo da janela mostra o nome do arquivo e um indicador de modificado (*)
 *   - Barra de status com mensagens rapidas
 *   - Atalhos de teclado: Ctrl+N novo, Ctrl+O abrir, Ctrl+S salvar,
 *                          Ctrl+Shift+S salvar como, Ctrl+Q sair,
 *			   Ctrl+F localizar e Ctrl+H substituir
 *
 * Dependencias: GTK 3 (libgtk-3-dev)
 *
 * Compilar:
 *   gcc -Wall -Wextra -o editor editor.c $(pkg-config --cflags --libs gtk+-3.0)
 *
 * Executar:
 *   ./editor
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
    GtkWidget *pos_label;  /* Rotulo com linha/coluna do cursor, canto inferior direito */

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
    snprintf(title, sizeof(title), "%s%s - Editor de Texto",
             base, app->modified ? " *" : "");
    gtk_window_set_title(GTK_WINDOW(app->window), title);
}

/* Atualiza o rotulo de linha/coluna com base na posicao atual do cursor
 * (marca "insert" do buffer). Linha e coluna sao exibidas a partir de 1. */
static void update_cursor_position(AppState *app) {
    GtkTextIter iter;
    GtkTextMark *insert_mark = gtk_text_buffer_get_insert(app->buffer);
    gtk_text_buffer_get_iter_at_mark(app->buffer, &iter, insert_mark);

    gint linha = gtk_text_iter_get_line(&iter) + 1;
    gint coluna = gtk_text_iter_get_line_offset(&iter) + 1;

    char texto[64];
    snprintf(texto, sizeof(texto), "Linha: %d, Coluna: %d", linha, coluna);
    gtk_label_set_text(GTK_LABEL(app->pos_label), texto);
}

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data) {
    (void)buffer;
    AppState *app = (AppState *)user_data;
    if (!app->modified) {
        app->modified = TRUE;
        update_title(app);
    }
    /* Qualquer edição (digitar, apagar, colar, desfazer/refazer) move o
     * cursor, entao atualizamos a posicao tambem aqui, e nao apenas em
     * "mark-set", para garantir que nenhuma tecla passe sem atualizar. */
    update_cursor_position(app);
}

/* Disparado sempre que uma marca do buffer muda de posicao (inclui cliques
 * do mouse e navegacao com as setas/Home/End/Page Up/Page Down, que nao
 * alteram o texto e portanto nao passam por on_buffer_changed). So nos
 * interessa a marca "insert". */
static void on_cursor_moved(GtkTextBuffer *buffer, GtkTextIter *location,
                             GtkTextMark *mark, gpointer user_data) {
    (void)location;
    AppState *app = (AppState *)user_data;
    if (mark == gtk_text_buffer_get_insert(buffer)) {
        update_cursor_position(app);
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

    /* Mesma cautela do save_file: duplicar antes de liberar, caso 'path'
     * algum dia aponte para o mesmo buffer de app->filename. */
    gchar *novo_nome = g_strdup(path);
    g_free(app->filename);
    app->filename = novo_nome;
    app->modified = FALSE;
    update_title(app);

    char status[600];
    snprintf(status, sizeof(status), "Arquivo aberto: %s", app->filename);
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

    /* IMPORTANTE: 'path' pode ser o proprio app->filename (quando o botao
     * "Salvar" reutiliza o nome ja aberto). Por isso duplicamos 'path'
     * ANTES de liberar app->filename — se liberassemos primeiro, 'path'
     * ficaria com memoria invalida e o nome original do arquivo seria
     * perdido/corrompido. */
    gchar *novo_nome = g_strdup(path);
    g_free(app->filename);
    app->filename = novo_nome;
    app->modified = FALSE;
    update_title(app);

    /* Usamos app->filename (ja valido e atualizado) na mensagem, e nao
     * 'path': se 'path' for o mesmo ponteiro que app->filename tinha antes,
     * ele ja foi liberado pelo g_free acima e mostraria um nome corrompido. */
    char status[600];
    snprintf(status, sizeof(status), "Arquivo salvo: %s", app->filename);
    set_status(app, status);
    return TRUE;
}

/* ---------- Busca de texto ---------- */

/* Procura 'termo' a partir da posicao atual do cursor. Se nao encontrar
 * dali ate o fim, volta ao inicio do documento e tenta de novo (busca
 * circular). Se encontrado, seleciona o trecho e rola a view ate ele. */
static gboolean find_text(AppState *app, const char *termo, gboolean *deu_a_volta) {
    if (!termo || termo[0] == '\0') return FALSE;

    GtkTextIter cursor_iter, start, end;
    GtkTextMark *insert_mark = gtk_text_buffer_get_insert(app->buffer);
    gtk_text_buffer_get_iter_at_mark(app->buffer, &cursor_iter, insert_mark);

    GtkTextSearchFlags flags = GTK_TEXT_SEARCH_CASE_INSENSITIVE | GTK_TEXT_SEARCH_TEXT_ONLY;

    gboolean achou = gtk_text_iter_forward_search(&cursor_iter, termo, flags,
                                                   &start, &end, NULL);
    *deu_a_volta = FALSE;
    if (!achou) {
        /* Tenta novamente a partir do inicio do documento (busca circular) */
        GtkTextIter inicio;
        gtk_text_buffer_get_start_iter(app->buffer, &inicio);
        achou = gtk_text_iter_forward_search(&inicio, termo, flags, &start, &end, NULL);
        *deu_a_volta = achou;
    }

    if (achou) {
        /* gtk_text_buffer_select_range(buffer, ins, bound) coloca a marca
         * "insert" em 'ins' e "selection_bound" em 'bound'. Passando
         * (&end, &start) em vez de (&start, &end), a selecao visual e a
         * mesma, mas o cursor ("insert") fica no FIM do trecho encontrado.
         * Assim, a proxima chamada de find_text comeca a busca depois da
         * ocorrencia atual e avanca pelo texto, em vez de ficar presa
         * repetindo sempre o mesmo resultado (que aconteceria se o cursor
         * ficasse no inicio do trecho). */
        gtk_text_buffer_select_range(app->buffer, &end, &start);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->text_view), &start,
                                      0.0, FALSE, 0.0, 0.0);
    }
    return achou;
}

static void action_find(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    AppState *app = (AppState *)user_data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Localizar", GTK_WINDOW(app->window), GTK_DIALOG_MODAL,
        "_Fechar", GTK_RESPONSE_CLOSE,
        "_Localizar proximo", GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 6);

    GtkWidget *label = gtk_label_new("Texto:");
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);

    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(content), hbox);
    gtk_widget_show_all(dialog);

    gint resp;
    do {
        resp = gtk_dialog_run(GTK_DIALOG(dialog));
        if (resp == GTK_RESPONSE_ACCEPT) {
            const char *termo = gtk_entry_get_text(GTK_ENTRY(entry));
            gboolean deu_a_volta = FALSE;
            gboolean achou = find_text(app, termo, &deu_a_volta);

            char status[300];
            if (!achou) {
                snprintf(status, sizeof(status), "Nao encontrado: \"%s\"", termo);
            } else if (deu_a_volta) {
                snprintf(status, sizeof(status), "Encontrado (voltou ao inicio): \"%s\"", termo);
            } else {
                snprintf(status, sizeof(status), "Encontrado: \"%s\"", termo);
            }
            set_status(app, status);
        }
    } while (resp == GTK_RESPONSE_ACCEPT);

    gtk_widget_destroy(dialog);
}

/* Compara duas strings ignorando maiusculas/minusculas de forma correta
 * para UTF-8 (acentos inclusive). Retorna TRUE se forem iguais. */
static gboolean texto_igual_sem_case(const char *a, const char *b) {
    if (!a || !b) return FALSE;
    gchar *fa = g_utf8_casefold(a, -1);
    gchar *fb = g_utf8_casefold(b, -1);
    gboolean iguais = (g_strcmp0(fa, fb) == 0);
    g_free(fa);
    g_free(fb);
    return iguais;
}

/* Substitui todas as ocorrencias de 'termo' por 'substituicao' no buffer
 * inteiro. Retorna a quantidade de substituicoes realizadas. */
static gint replace_all(AppState *app, const char *termo, const char *substituicao) {
    if (!termo || termo[0] == '\0') return 0;

    GtkTextSearchFlags flags = GTK_TEXT_SEARCH_CASE_INSENSITIVE | GTK_TEXT_SEARCH_TEXT_ONLY;
    GtkTextIter iter, start, end;
    gint count = 0;

    gtk_text_buffer_get_start_iter(app->buffer, &iter);
    while (gtk_text_iter_forward_search(&iter, termo, flags, &start, &end, NULL)) {
        gtk_text_buffer_delete(app->buffer, &start, &end);
        gtk_text_buffer_insert(app->buffer, &start, substituicao, -1);
        /* Apos a insercao, 'start' aponta para o fim do texto inserido;
         * continuamos a busca a partir dali para nao entrar em loop
         * infinito quando a substituicao contem o proprio termo. */
        iter = start;
        count++;
    }
    return count;
}

static void action_replace(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    AppState *app = (AppState *)user_data;

    enum { RESP_FIND_NEXT = 1, RESP_REPLACE = 2, RESP_REPLACE_ALL = 3 };

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Substituir", GTK_WINDOW(app->window), GTK_DIALOG_MODAL,
        "_Fechar", GTK_RESPONSE_CLOSE,
        "_Localizar proximo", RESP_FIND_NEXT,
        "_Substituir", RESP_REPLACE,
        "Substituir _todos", RESP_REPLACE_ALL,
        NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), RESP_FIND_NEXT);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 6);

    GtkWidget *label_find = gtk_label_new("Localizar:");
    gtk_widget_set_halign(label_find, GTK_ALIGN_START);
    GtkWidget *entry_find = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry_find), TRUE);

    GtkWidget *label_replace = gtk_label_new("Substituir por:");
    gtk_widget_set_halign(label_replace, GTK_ALIGN_START);
    GtkWidget *entry_replace = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry_replace), TRUE);

    gtk_grid_attach(GTK_GRID(grid), label_find, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_find, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_replace, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_replace, 1, 1, 1, 1);
    gtk_widget_set_hexpand(entry_find, TRUE);
    gtk_widget_set_hexpand(entry_replace, TRUE);

    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dialog);

    gint resp;
    do {
        resp = gtk_dialog_run(GTK_DIALOG(dialog));
        const char *termo = gtk_entry_get_text(GTK_ENTRY(entry_find));
        const char *substituicao = gtk_entry_get_text(GTK_ENTRY(entry_replace));
        char status[300];

        if (resp == RESP_FIND_NEXT) {
            gboolean deu_a_volta = FALSE;
            gboolean achou = find_text(app, termo, &deu_a_volta);
            if (!achou) {
                snprintf(status, sizeof(status), "Nao encontrado: \"%s\"", termo);
            } else {
                snprintf(status, sizeof(status), "Encontrado: \"%s\"", termo);
            }
            set_status(app, status);

        } else if (resp == RESP_REPLACE) {
            GtkTextIter sel_start, sel_end;
            gboolean tem_selecao = gtk_text_buffer_get_selection_bounds(
                app->buffer, &sel_start, &sel_end);
            gchar *selecionado = tem_selecao
                ? gtk_text_buffer_get_text(app->buffer, &sel_start, &sel_end, FALSE)
                : NULL;

            if (tem_selecao && texto_igual_sem_case(selecionado, termo)) {
                /* O trecho ja selecionado e uma ocorrencia: substitui e
                 * posiciona o cursor logo apos, para permitir continuar. */
                gtk_text_buffer_delete(app->buffer, &sel_start, &sel_end);
                gtk_text_buffer_insert(app->buffer, &sel_start, substituicao, -1);
                snprintf(status, sizeof(status), "Substituido: \"%s\" -> \"%s\"",
                         termo, substituicao);
                set_status(app, status);
                /* Ja avanca para a proxima ocorrencia */
                gboolean deu_a_volta = FALSE;
                find_text(app, termo, &deu_a_volta);
            } else {
                /* Nada selecionado corresponde ao termo: apenas localiza a
                 * proxima ocorrencia, e o usuario clica "Substituir" de novo. */
                gboolean deu_a_volta = FALSE;
                gboolean achou = find_text(app, termo, &deu_a_volta);
                snprintf(status, sizeof(status), achou
                         ? "Encontrado: \"%s\" (clique Substituir novamente)"
                         : "Nao encontrado: \"%s\"", termo);
                set_status(app, status);
            }
            g_free(selecionado);

        } else if (resp == RESP_REPLACE_ALL) {
            gint count = replace_all(app, termo, substituicao);
            snprintf(status, sizeof(status),
                     "%d ocorrencia(s) de \"%s\" substituida(s) por \"%s\"",
                     count, termo, substituicao);
            set_status(app, status);
        }
    } while (resp == RESP_FIND_NEXT || resp == RESP_REPLACE || resp == RESP_REPLACE_ALL);

    gtk_widget_destroy(dialog);
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
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "Editor de Texto");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "1.1.3");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog),
        "Um editor de texto simples e leve, escrito em C com GTK3.\n\n"
        "Permite abrir, editar e salvar arquivos de texto (.txt e similares) "
        "atraves de uma interface grafica com toolbar, area de edição e "
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
        case GDK_KEY_f: case GDK_KEY_F:
            action_find(NULL, app);
            return TRUE;
        case GDK_KEY_h: case GDK_KEY_H:
            action_replace(NULL, app);
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

    GtkToolItem *btn_find = make_tool_button("Localizar", "edit-find",
                                              G_CALLBACK(action_find), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), btn_find, -1);

    GtkToolItem *btn_replace = make_tool_button("Substituir", "edit-find-replace",
                                                 G_CALLBACK(action_replace), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), btn_replace, -1);

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
    g_signal_connect(app.buffer, "mark-set", G_CALLBACK(on_cursor_moved), &app);

    /* Barra de status + rotulo de linha/coluna (canto inferior direito) */
    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    app.statusbar = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(status_box), app.statusbar, TRUE, TRUE, 0);

    app.pos_label = gtk_label_new("Linha: 1, Coluna: 1");
    gtk_widget_set_margin_start(app.pos_label, 8);
    gtk_widget_set_margin_end(app.pos_label, 8);
    gtk_box_pack_end(GTK_BOX(status_box), app.pos_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), status_box, FALSE, FALSE, 0);

    app.statusbar_ctx = gtk_statusbar_get_context_id(GTK_STATUSBAR(app.statusbar), "geral");
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
