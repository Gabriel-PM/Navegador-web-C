/* ============================================================
 *  NAVEGADOR WEB SIMPLIFICADO  -  Trabalho de Faculdade
 *  Linguagem: C (padrao C99)
 *
 *  O que ele faz:
 *    1. Le uma URL digitada pelo usuario (ex: http://example.com)
 *    2. Separa a URL em host, porta e caminho
 *    3. Abre uma conexao TCP com o servidor (sockets)
 *    4. Envia uma requisicao HTTP GET
 *    5. Recebe a resposta, separa cabecalho e corpo
 *    6. "Renderiza" o HTML: remove as tags e mostra so o texto
 *    7. Guarda historico, permite voltar, ver codigo-fonte e salvar
 *
 *  Agora com suporte a HTTPS via OpenSSL: quando a URL comeca com
 *  https://, a conexao TCP normal e "embrulhada" numa camada TLS
 *  antes de enviar/receber os dados. Para HTTP puro, nada muda.
 *
 *  Como compilar:
 *    Linux / Mac / WSL:  gcc navegador.c -o navegador -lssl -lcrypto
 *    Windows (MinGW):    gcc navegador.c -o navegador.exe -lssl -lcrypto -lws2_32
 *
 *  Se faltar a biblioteca no Linux, instale com:
 *    sudo apt install libssl-dev
 * ============================================================ */

/* Necessario no Linux para liberar getaddrinfo() no modo C99 padrao */
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200112L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Bibliotecas do OpenSSL: cuidam de toda a criptografia do HTTPS */
#include <openssl/ssl.h>
#include <openssl/err.h>

/* ---- Compatibilidade entre Windows e Linux ----------------- */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET socket_t;
    #define FECHAR_SOCKET closesocket
    #define SOCKET_INVALIDO INVALID_SOCKET
#else
    #include <unistd.h>
    #include <netdb.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    typedef int socket_t;
    #define FECHAR_SOCKET close
    #define SOCKET_INVALIDO (-1)
#endif

/* ---- Constantes -------------------------------------------- */
#define TAM_URL        512
#define TAM_HOST       256
#define TAM_CAMINHO    512
#define TAM_PORTA      8
#define MAX_HISTORICO  50
#define MAX_REDIRECTS  5

/* ---- Variaveis globais do historico ------------------------ */
char historico[MAX_HISTORICO][TAM_URL];
int  total_historico = 0;

/* ============================================================
 *  PARTE 1 - ANALISE DA URL
 *  Quebra "http://exemplo.com:80/pagina.html" em tres partes.
 *  Retorna 1 se deu certo, 0 se a URL for invalida.
 * ============================================================ */
int analisar_url(const char *url, char *host, char *porta, char *caminho, int *usa_https)
{
    const char *p = url;
    int i;

    *usa_https = 0;

    /* Pula o "http://" ou "https://" se existir, e marca qual e' */
    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        *usa_https = 1;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    }

    /* Copia o host ate encontrar ':' (porta) ou '/' (caminho) */
    i = 0;
    while (*p && *p != ':' && *p != '/' && i < TAM_HOST - 1) {
        host[i++] = *p++;
    }
    host[i] = '\0';

    if (i == 0) {
        printf("\n[ERRO] URL invalida: nao consegui identificar o servidor.\n");
        return 0;
    }

    /* Se veio ':', o que segue e a porta */
    if (*p == ':') {
        p++;
        i = 0;
        while (isdigit((unsigned char)*p) && i < TAM_PORTA - 1) {
            porta[i++] = *p++;
        }
        porta[i] = '\0';
    } else {
        strcpy(porta, *usa_https ? "443" : "80");   /* porta padrao de cada protocolo */
    }

    /* O resto e o caminho. Se estiver vazio, usamos "/" */
    if (*p == '\0') {
        strcpy(caminho, "/");
    } else {
        i = 0;
        while (*p && i < TAM_CAMINHO - 1) {
            caminho[i++] = *p++;
        }
        caminho[i] = '\0';
    }

    return 1;
}

/* ============================================================
 *  PARTE 2 - CONEXAO E DOWNLOAD
 *  Abre o socket, envia o GET e devolve a resposta inteira
 *  (cabecalho + corpo) em memoria alocada com malloc.
 *  Quem chamar precisa dar free() depois.
 * ============================================================ */
char *baixar_pagina(const char *host, const char *porta, const char *caminho, int usa_https)
{
    struct addrinfo dicas, *resultado, *rp;
    socket_t sock = SOCKET_INVALIDO;
    char requisicao[1024];
    char *resposta = NULL;
    size_t capacidade = 8192;
    size_t tamanho = 0;
    int recebidos;

    /* Estas duas so' sao usadas quando usa_https == 1 */
    SSL_CTX *contexto_ssl = NULL;
    SSL *ssl = NULL;

    /* --- Resolve o nome do site para um endereco IP --- */
    memset(&dicas, 0, sizeof(dicas));
    dicas.ai_family   = AF_UNSPEC;      /* IPv4 ou IPv6 */
    dicas.ai_socktype = SOCK_STREAM;    /* TCP */

    printf("  -> Procurando o endereco de %s ...\n", host);
    if (getaddrinfo(host, porta, &dicas, &resultado) != 0) {
        printf("\n[ERRO] Nao encontrei o servidor '%s'. Verifique a internet ou o nome.\n", host);
        return NULL;
    }

    /* --- Tenta conectar em cada endereco ate um funcionar --- */
    printf("  -> Conectando na porta %s ...\n", porta);
    for (rp = resultado; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == SOCKET_INVALIDO) continue;

        if (connect(sock, rp->ai_addr, (int)rp->ai_addrlen) == 0) break;

        FECHAR_SOCKET(sock);
        sock = SOCKET_INVALIDO;
    }
    freeaddrinfo(resultado);

    if (sock == SOCKET_INVALIDO) {
        printf("\n[ERRO] Nao consegui conectar em %s:%s.\n", host, porta);
        return NULL;
    }

    /* --- Se for HTTPS, "embrulha" o socket numa camada TLS --- */
    if (usa_https) {
        printf("  -> Fazendo o aperto de mao TLS (handshake) ...\n");

        SSL_library_init();
        SSL_load_error_strings();

        contexto_ssl = SSL_CTX_new(TLS_client_method());
        if (contexto_ssl == NULL) {
            printf("\n[ERRO] Falha ao criar o contexto SSL.\n");
            FECHAR_SOCKET(sock);
            return NULL;
        }

        ssl = SSL_new(contexto_ssl);
        SSL_set_fd(ssl, (int)sock);

        /* SNI: informa ao servidor qual site queremos, pois um mesmo
         * IP pode hospedar varios dominios com certificados diferentes */
        SSL_set_tlsext_host_name(ssl, host);

        if (SSL_connect(ssl) != 1) {
            printf("\n[ERRO] Falha no handshake TLS com %s.\n", host);
            ERR_print_errors_fp(stdout);
            SSL_free(ssl);
            SSL_CTX_free(contexto_ssl);
            FECHAR_SOCKET(sock);
            return NULL;
        }
        printf("  -> Conexao segura estabelecida (%s).\n", SSL_get_version(ssl));
    }

    /* --- Monta e envia a requisicao HTTP ---
     * Usamos HTTP/1.1 porque muitos servidores modernos (inclusive
     * com HTTPS) recusam HTTP/1.0. Pedimos "Connection: close" para
     * o servidor fechar a conexao no final, assim sabemos quando a
     * pagina terminou sem precisar tratar "chunked encoding".
     */
    snprintf(requisicao, sizeof(requisicao),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: NavegadorSimples/1.0\r\n"
             "Accept: text/html\r\n"
             "Connection: close\r\n"
             "\r\n",
             caminho, host);

    printf("  -> Enviando requisicao GET %s\n", caminho);
    if (usa_https) {
        if (SSL_write(ssl, requisicao, (int)strlen(requisicao)) <= 0) {
            printf("\n[ERRO] Falha ao enviar a requisicao (SSL).\n");
            SSL_free(ssl);
            SSL_CTX_free(contexto_ssl);
            FECHAR_SOCKET(sock);
            return NULL;
        }
    } else {
        if (send(sock, requisicao, (int)strlen(requisicao), 0) < 0) {
            printf("\n[ERRO] Falha ao enviar a requisicao.\n");
            FECHAR_SOCKET(sock);
            return NULL;
        }
    }

    /* --- Recebe a resposta em pedacos, aumentando o buffer --- */
    resposta = (char *) malloc(capacidade);
    if (resposta == NULL) {
        printf("\n[ERRO] Memoria insuficiente.\n");
        if (usa_https) { SSL_free(ssl); SSL_CTX_free(contexto_ssl); }
        FECHAR_SOCKET(sock);
        return NULL;
    }

    printf("  -> Baixando ...\n");
    while (1) {
        if (usa_https) {
            recebidos = SSL_read(ssl, resposta + tamanho, (int)(capacidade - tamanho - 1));
        } else {
            recebidos = recv(sock, resposta + tamanho, (int)(capacidade - tamanho - 1), 0);
        }
        if (recebidos <= 0) break;

        tamanho += (size_t)recebidos;

        /* Buffer quase cheio? Dobra o tamanho (realloc) */
        if (tamanho + 1 >= capacidade) {
            char *maior;
            capacidade *= 2;
            maior = (char *) realloc(resposta, capacidade);
            if (maior == NULL) {
                printf("\n[ERRO] Memoria insuficiente durante o download.\n");
                free(resposta);
                if (usa_https) { SSL_free(ssl); SSL_CTX_free(contexto_ssl); }
                FECHAR_SOCKET(sock);
                return NULL;
            }
            resposta = maior;
        }
    }
    resposta[tamanho] = '\0';

    if (usa_https) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(contexto_ssl);
    }
    FECHAR_SOCKET(sock);
    printf("  -> Pronto! %lu bytes recebidos.\n", (unsigned long)tamanho);
    return resposta;
}

/* ============================================================
 *  PARTE 3 - LEITURA DA RESPOSTA HTTP
 * ============================================================ */

/* Le o codigo de status da primeira linha: "HTTP/1.1 200 OK" -> 200 */
int codigo_status(const char *resposta)
{
    const char *espaco = strchr(resposta, ' ');
    if (espaco == NULL) return 0;
    return atoi(espaco + 1);
}

/* O corpo comeca depois da linha em branco (\r\n\r\n) */
const char *extrair_corpo(const char *resposta)
{
    const char *separador = strstr(resposta, "\r\n\r\n");
    if (separador != NULL) return separador + 4;

    separador = strstr(resposta, "\n\n");   /* alguns servidores usam so \n */
    if (separador != NULL) return separador + 2;

    return resposta;   /* nao achou cabecalho, devolve tudo */
}

/* Procura um cabecalho (ex: "Location:") e copia o valor.
 * Retorna 1 se encontrou. */
int buscar_cabecalho(const char *resposta, const char *nome, char *destino, int tam_max)
{
    const char *inicio = strstr(resposta, nome);
    int i = 0;

    if (inicio == NULL) return 0;

    inicio += strlen(nome);
    while (*inicio == ' ') inicio++;          /* pula espacos */

    while (*inicio && *inicio != '\r' && *inicio != '\n' && i < tam_max - 1) {
        destino[i++] = *inicio++;
    }
    destino[i] = '\0';
    return 1;
}

/* ============================================================
 *  PARTE 4 - "RENDERIZADOR" DE HTML
 *  Nao desenha nada na tela graficamente: ele remove as tags
 *  e imprime apenas o texto, que e o conteudo que o usuario le.
 * ============================================================ */

/* Traduz as entidades HTML mais comuns.
 * O texto de saida ja sai em UTF-8 (por isso os acentos sao strings).
 * Retorna quantos caracteres da entidade foram consumidos, ou 0. */
int traduzir_entidade(const char *texto, const char **saida)
{
    static const struct { const char *entidade; const char *texto_saida; } tabela[] = {
        { "&amp;", "&" },  { "&lt;", "<" },   { "&gt;", ">" },
        { "&quot;", "\"" },{ "&apos;", "'" }, { "&#39;", "'" },
        { "&nbsp;", " " }, { "&hellip;", "..." }, { "&mdash;", "--" },
        { "&ndash;", "-" },
        { "&aacute;", "\u00e1" }, { "&eacute;", "\u00e9" }, { "&iacute;", "\u00ed" },
        { "&oacute;", "\u00f3" }, { "&uacute;", "\u00fa" },
        { "&acirc;",  "\u00e2" }, { "&ecirc;",  "\u00ea" }, { "&ocirc;",  "\u00f4" },
        { "&atilde;", "\u00e3" }, { "&otilde;", "\u00f5" }, { "&ccedil;", "\u00e7" },
        { "&agrave;", "\u00e0" },
        { "&Aacute;", "\u00c1" }, { "&Eacute;", "\u00c9" }, { "&Iacute;", "\u00cd" },
        { "&Oacute;", "\u00d3" }, { "&Uacute;", "\u00da" }, { "&Ccedil;", "\u00c7" },
        { "&Atilde;", "\u00c3" }, { "&Otilde;", "\u00d5" }
    };
    int total = (int)(sizeof(tabela) / sizeof(tabela[0]));
    int i;

    for (i = 0; i < total; i++) {
        size_t tam = strlen(tabela[i].entidade);
        if (strncmp(texto, tabela[i].entidade, tam) == 0) {
            *saida = tabela[i].texto_saida;
            return (int)tam;
        }
    }
    return 0;   /* nao e uma entidade conhecida */
}

/* Compara tag ignorando maiusculas/minusculas */
int tag_comeca_com(const char *html, const char *nome)
{
    size_t i;
    for (i = 0; nome[i] != '\0'; i++) {
        if (tolower((unsigned char)html[i]) != tolower((unsigned char)nome[i]))
            return 0;
    }
    return 1;
}

void renderizar_html(const char *html)
{
    int dentro_da_tag = 0;
    int espaco_pendente = 0;
    int quebras_seguidas = 2;   /* evita linhas em branco no comeco */
    char titulo[256];
    const char *p;
    const char *tag_titulo;

    printf("\n===============================================================\n");

    /* --- Mostra o <title> da pagina, se existir --- */
    tag_titulo = strstr(html, "<title>");
    if (tag_titulo == NULL) tag_titulo = strstr(html, "<TITLE>");
    if (tag_titulo != NULL) {
        int i = 0;
        tag_titulo += 7;
        while (*tag_titulo && *tag_titulo != '<' && i < 255) {
            titulo[i++] = *tag_titulo++;
        }
        titulo[i] = '\0';
        printf(" TITULO: %s\n", titulo);
        printf("===============================================================\n");
    }

    /* --- Percorre o HTML caractere por caractere --- */
    for (p = html; *p != '\0'; p++) {

        /* Ignora comentarios <!-- ... --> */
        if (strncmp(p, "<!--", 4) == 0) {
            const char *fim = strstr(p, "-->");
            if (fim == NULL) break;
            p = fim + 2;
            continue;
        }

        if (*p == '<') {
            const char *interior = p + 1;

            /* <script> e <style> tem codigo dentro: pula tudo ate fechar */
            if (tag_comeca_com(interior, "script")) {
                const char *fim = strstr(p, "</script");
                if (fim == NULL) fim = strstr(p, "</SCRIPT");
                if (fim == NULL) break;
                p = fim - 1;        /* -1 porque o for vai somar 1 */
                continue;
            }
            if (tag_comeca_com(interior, "style")) {
                const char *fim = strstr(p, "</style");
                if (fim == NULL) fim = strstr(p, "</STYLE");
                if (fim == NULL) break;
                p = fim - 1;
                continue;
            }
            /* O titulo ja foi mostrado no cabecalho, entao pulamos aqui */
            if (tag_comeca_com(interior, "title")) {
                const char *fim = strstr(p, "</title");
                if (fim == NULL) fim = strstr(p, "</TITLE");
                if (fim == NULL) break;
                p = fim - 1;
                continue;
            }

            /* Tags de bloco geram quebra de linha na saida */
            if (tag_comeca_com(interior, "br")   || tag_comeca_com(interior, "p>")  ||
                tag_comeca_com(interior, "/p")   || tag_comeca_com(interior, "div") ||
                tag_comeca_com(interior, "/div") || tag_comeca_com(interior, "li")  ||
                tag_comeca_com(interior, "/h")   || tag_comeca_com(interior, "tr")  ||
                tag_comeca_com(interior, "/tr")) {
                if (quebras_seguidas < 2) {
                    printf("\n");
                    quebras_seguidas++;
                }
                espaco_pendente = 0;
            }

            dentro_da_tag = 1;
            continue;
        }

        if (*p == '>') {
            dentro_da_tag = 0;
            continue;
        }

        if (dentro_da_tag) continue;   /* estamos dentro de uma tag: ignora */

        /* --- Aqui e texto de verdade --- */
        if (*p == '&') {
            const char *convertido;
            int consumidos = traduzir_entidade(p, &convertido);
            if (consumidos > 0) {
                /* imprime o espaco que estava guardado antes da entidade */
                if (espaco_pendente && quebras_seguidas == 0) putchar(' ');
                espaco_pendente = 0;

                fputs(convertido, stdout);
                quebras_seguidas = 0;
                p += consumidos - 1;
                continue;
            }
        }

        if (isspace((unsigned char)*p)) {
            espaco_pendente = 1;       /* varios espacos viram um so */
            continue;
        }

        if (espaco_pendente && quebras_seguidas == 0) {
            putchar(' ');
            espaco_pendente = 0;
        }
        espaco_pendente = 0;

        putchar(*p);
        quebras_seguidas = 0;
    }

    printf("\n===============================================================\n");
}

/* ============================================================
 *  PARTE 5 - HISTORICO
 * ============================================================ */
void adicionar_historico(const char *url)
{
    if (total_historico < MAX_HISTORICO) {
        strncpy(historico[total_historico], url, TAM_URL - 1);
        historico[total_historico][TAM_URL - 1] = '\0';
        total_historico++;
    } else {
        /* Historico cheio: descarta o mais antigo */
        int i;
        for (i = 0; i < MAX_HISTORICO - 1; i++) {
            strcpy(historico[i], historico[i + 1]);
        }
        strncpy(historico[MAX_HISTORICO - 1], url, TAM_URL - 1);
        historico[MAX_HISTORICO - 1][TAM_URL - 1] = '\0';
    }
}

void mostrar_historico(void)
{
    int i;
    printf("\n--- HISTORICO DE NAVEGACAO ---\n");
    if (total_historico == 0) {
        printf("(vazio)\n");
        return;
    }
    for (i = 0; i < total_historico; i++) {
        printf("%2d. %s\n", i + 1, historico[i]);
    }
}

/* ============================================================
 *  PARTE 6 - FUNCAO QUE JUNTA TUDO: ACESSAR UMA URL
 *  Devolve a resposta completa (para poder ver o codigo-fonte
 *  depois) ou NULL em caso de erro.
 * ============================================================ */
char *acessar(const char *url_original, char *url_final)
{
    char host[TAM_HOST], porta[TAM_PORTA], caminho[TAM_CAMINHO];
    char url[TAM_URL];
    char *resposta;
    int tentativas = 0;
    int status;
    int usa_https;

    strncpy(url, url_original, TAM_URL - 1);
    url[TAM_URL - 1] = '\0';

    /* Laco de redirecionamento: se o servidor responder 301/302,
     * seguimos para o novo endereco (ate MAX_REDIRECTS vezes). */
    while (tentativas < MAX_REDIRECTS) {

        if (!analisar_url(url, host, porta, caminho, &usa_https)) return NULL;

        printf("\n[Acessando] %s (%s)\n", url, usa_https ? "seguro - HTTPS" : "sem criptografia - HTTP");
        resposta = baixar_pagina(host, porta, caminho, usa_https);
        if (resposta == NULL) return NULL;

        status = codigo_status(resposta);
        printf("  -> Status HTTP: %d\n", status);

        if (status == 301 || status == 302 || status == 307 || status == 308) {
            char novo[TAM_URL];
            if (buscar_cabecalho(resposta, "Location:", novo, TAM_URL)) {
                printf("  -> Redirecionado para: %s\n", novo);
                free(resposta);

                /* Se o Location for relativo (comeca com /), remonta a URL */
                if (novo[0] == '/') {
                    char montada[TAM_URL * 2];
                    snprintf(montada, sizeof(montada), "%s://%s%s",
                             usa_https ? "https" : "http", host, novo);
                    strncpy(url, montada, TAM_URL - 1);
                    url[TAM_URL - 1] = '\0';
                } else {
                    strncpy(url, novo, TAM_URL - 1);
                    url[TAM_URL - 1] = '\0';
                }
                tentativas++;
                continue;
            }
        }

        if (status == 404) {
            printf("  -> Pagina nao encontrada (erro 404).\n");
        } else if (status >= 400) {
            printf("  -> O servidor retornou um erro.\n");
        }

        strcpy(url_final, url);
        return resposta;
    }

    printf("\n[ERRO] Redirecionamentos demais. Desisti.\n");
    return NULL;
}

/* ============================================================
 *  PARTE 7 - FUNCOES AUXILIARES DO MENU
 * ============================================================ */

/* Le uma linha do teclado com seguranca e tira o '\n' do final */
void ler_linha(char *destino, int tamanho)
{
    if (fgets(destino, tamanho, stdin) != NULL) {
        destino[strcspn(destino, "\r\n")] = '\0';
    } else {
        destino[0] = '\0';
    }
}

void salvar_em_arquivo(const char *conteudo, const char *nome_arquivo)
{
    FILE *arq = fopen(nome_arquivo, "w");
    if (arq == NULL) {
        printf("\n[ERRO] Nao consegui criar o arquivo '%s'.\n", nome_arquivo);
        return;
    }
    fputs(conteudo, arq);
    fclose(arq);
    printf("\n[OK] Pagina salva em '%s'.\n", nome_arquivo);
}

void mostrar_menu(void)
{
    printf("\n+-------------------------------------------+\n");
    printf("|          NAVEGADOR WEB SIMPLES            |\n");
    printf("+-------------------------------------------+\n");
    printf("| 1 - Abrir uma URL                         |\n");
    printf("| 2 - Recarregar a pagina atual             |\n");
    printf("| 3 - Ver historico                         |\n");
    printf("| 4 - Voltar (pagina anterior)              |\n");
    printf("| 5 - Ver codigo-fonte HTML                 |\n");
    printf("| 6 - Salvar pagina em arquivo              |\n");
    printf("| 0 - Sair                                  |\n");
    printf("+-------------------------------------------+\n");
    printf("Escolha uma opcao: ");
}

/* ============================================================
 *  FUNCAO PRINCIPAL
 * ============================================================ */
int main(void)
{
    char entrada[TAM_URL];
    char url_atual[TAM_URL] = "";
    char *pagina_atual = NULL;   /* resposta HTTP completa da pagina aberta */
    int opcao;

#ifdef _WIN32
    /* No Windows e obrigatorio inicializar a biblioteca de sockets */
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[ERRO] Falha ao iniciar o Winsock.\n");
        return 1;
    }
#endif

    printf("\n*** Navegador Web Simplificado em C ***\n");
    printf("Suporta HTTP e HTTPS (via OpenSSL).\n");
    printf("Sugestoes para testar: https://example.com  |  http://info.cern.ch\n");

    do {
        mostrar_menu();
        ler_linha(entrada, sizeof(entrada));
        opcao = atoi(entrada);

        switch (opcao) {

            case 1: {   /* Abrir URL */
                char *nova;
                printf("\nDigite a URL: ");
                ler_linha(entrada, sizeof(entrada));

                if (entrada[0] == '\0') {
                    printf("\n[AVISO] Voce nao digitou nada.\n");
                    break;
                }

                nova = acessar(entrada, url_atual);
                if (nova != NULL) {
                    if (pagina_atual) free(pagina_atual);
                    pagina_atual = nova;
                    adicionar_historico(url_atual);
                    renderizar_html(extrair_corpo(pagina_atual));
                }
                break;
            }

            case 2: {   /* Recarregar */
                char *nova;
                if (url_atual[0] == '\0') {
                    printf("\n[AVISO] Nenhuma pagina aberta ainda.\n");
                    break;
                }
                nova = acessar(url_atual, url_atual);
                if (nova != NULL) {
                    if (pagina_atual) free(pagina_atual);
                    pagina_atual = nova;
                    renderizar_html(extrair_corpo(pagina_atual));
                }
                break;
            }

            case 3:     /* Historico */
                mostrar_historico();
                break;

            case 4: {   /* Voltar */
                char *nova;
                if (total_historico < 2) {
                    printf("\n[AVISO] Nao ha pagina anterior.\n");
                    break;
                }
                total_historico--;                    /* descarta a atual */
                strcpy(entrada, historico[total_historico - 1]);
                total_historico--;                    /* sera readicionada */

                nova = acessar(entrada, url_atual);
                if (nova != NULL) {
                    if (pagina_atual) free(pagina_atual);
                    pagina_atual = nova;
                    adicionar_historico(url_atual);
                    renderizar_html(extrair_corpo(pagina_atual));
                }
                break;
            }

            case 5:     /* Codigo-fonte */
                if (pagina_atual == NULL) {
                    printf("\n[AVISO] Nenhuma pagina aberta ainda.\n");
                } else {
                    printf("\n--- CODIGO-FONTE (resposta HTTP completa) ---\n");
                    printf("%s\n", pagina_atual);
                }
                break;

            case 6:     /* Salvar */
                if (pagina_atual == NULL) {
                    printf("\n[AVISO] Nenhuma pagina aberta ainda.\n");
                } else {
                    printf("\nNome do arquivo (ex: pagina.html): ");
                    ler_linha(entrada, sizeof(entrada));
                    if (entrada[0] == '\0') strcpy(entrada, "pagina.html");
                    salvar_em_arquivo(extrair_corpo(pagina_atual), entrada);
                }
                break;

            case 0:
                printf("\nEncerrando o navegador. Ate mais!\n");
                break;

            default:
                printf("\n[AVISO] Opcao invalida. Escolha um numero do menu.\n");
        }

    } while (opcao != 0);

    /* Libera a memoria antes de sair */
    if (pagina_atual) free(pagina_atual);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
