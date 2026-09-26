# Editor de Texto
Editor de texto simples com interface grafica (GTK3)

Funcionalidades:
   - Abrir arquivo de texto (dialogo de selecao de arquivo)
   - Editar o texto livremente em uma area de edição (GtkTextView)
   - Salvar / Salvar como
   - Novo documento
   - Aviso ao fechar/abrir/novo se houver alteracoes nao salvas
   - Titulo da janela mostra o nome do arquivo e um indicador de modificado (*)
   - Barra de status com mensagens rapidas
   - Atalhos de teclado: Ctrl+N novo, Ctrl+O abrir, Ctrl+S salvar, Ctrl+Shift+S salvar como, Ctrl+Q sair, Ctrl+F localizar e Ctrl+H substituir

 Dependencias: GTK 3 (libgtk-3-dev)

 Compilar:
   gcc -Wall -Wextra -o editor editor.c $(pkg-config --cflags --libs gtk+-3.0)

 Executar:
   ./editor
