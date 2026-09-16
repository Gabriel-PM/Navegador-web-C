# Navegador Web Simplificado em C

Trabalho da disciplina de Engenharia de Software — Curso de Engenharia de Software, UNIALFA (Centro Universitário Alves Faria).

**Autor:** Gabriel Pereira Martins

---

## Sumário

- [Objetivo](#objetivo)
- [Funcionalidades](#funcionalidades)
- [Limitações conhecidas](#limitações-conhecidas)
- [Arquitetura geral](#arquitetura-geral)
- [Descrição das funções principais](#descrição-das-funções-principais)
- [Suporte a HTTPS (TLS/SSL)](#suporte-a-https-tlsssl)
- [Interpretação do HTML](#interpretação-renderização-do-html)
- [Testes realizados](#testes-realizados)
- [Como compilar e executar](#como-compilar-e-executar)
- [Conclusão](#conclusão)

---

## Objetivo

Este projeto implementa, em linguagem C, um navegador web simplificado que roda em modo texto (linha de comando). O objetivo é demonstrar na prática os conceitos que um navegador de verdade (Chrome, Firefox, Edge) utiliza por trás da interface gráfica:

- Comunicação em rede via **sockets TCP**
- Requisições usando o protocolo **HTTP/1.1**
- Conexões seguras via **HTTPS** (TLS), usando a biblioteca **OpenSSL**
- Tratamento de **redirecionamentos** (301, 302, 307, 308)
- Um interpretador simplificado de **HTML**, que remove tags e exibe apenas o texto visível ao usuário
- Gerenciamento de memória dinâmica (`malloc`/`realloc`) para lidar com páginas de tamanho variável

Diferente de um navegador comercial, esta aplicação não possui interface gráfica, não renderiza layout, imagens ou CSS. O foco está na camada de rede e no processamento do conteúdo — etapas que normalmente ficam escondidas dentro dos motores de renderização dos navegadores reais.

## Funcionalidades

- Abrir uma URL (HTTP ou HTTPS)
- Recarregar a página atual
- Ver histórico de navegação
- Voltar para a página anterior
- Ver o código-fonte HTML da página
- Salvar a página em um arquivo

## Limitações conhecidas

Por ser um trabalho acadêmico com escopo reduzido, o programa **não implementa**:

- Renderização gráfica de layout, imagens ou vídeos
- Execução de JavaScript
- Aplicação de folhas de estilo CSS
- Suporte a cookies e cache de páginas
- Múltiplas abas
- Protocolo HTTP/2

Além disso, a tradução de entidades HTML cobre apenas os casos mais comuns (`&amp;`, `&aacute;`, etc.); entidades numéricas (ex.: `&#231;`) e algumas entidades nomeadas menos comuns (ex.: `&copy;`) ainda não são convertidas.

## Arquitetura geral

O programa foi organizado em módulos funcionais dentro de um único arquivo-fonte (`navegador.c`), cada um responsável por uma etapa do processo de "navegar" até uma página:

```
Usuário digita a URL
        |
        v
[1] analisar_url()      -> separa protocolo, host, porta e caminho
        |
        v
[2] baixar_pagina()     -> abre socket TCP, faz handshake TLS (se HTTPS),
                           envia o GET e recebe a resposta
        |
        v
[3] codigo_status()     -> lê o código HTTP (200, 301, 404, ...)
    buscar_cabecalho()  -> segue redirecionamentos, se houver
        |
        v
[4] extrair_corpo()     -> separa o cabeçalho HTTP do conteúdo HTML
        |
        v
[5] renderizar_html()   -> remove tags, converte entidades e imprime o texto
        |
        v
[6] adicionar_historico() -> guarda a URL para permitir voltar depois
```

O código utiliza diretivas de compilação condicional (`#ifdef _WIN32`) para funcionar tanto em Linux/macOS (API POSIX de sockets) quanto em Windows (API Winsock), sem precisar manter dois arquivos separados.

## Descrição das funções principais

| Função | Assinatura | O que faz |
|---|---|---|
| `analisar_url` | `int (const char *url, char *host, char *porta, char *caminho, int *usa_https)` | Separa a URL digitada em host, porta, caminho e identifica se o protocolo é HTTP ou HTTPS. |
| `baixar_pagina` | `char * (const char *host, const char *porta, const char *caminho, int usa_https)` | Resolve o DNS, abre o socket TCP, realiza o handshake TLS (quando HTTPS) e envia/recebe os dados via HTTP. |
| `codigo_status` | `int (const char *resposta)` | Extrai o código de status HTTP (200, 404, 301...) da primeira linha da resposta. |
| `extrair_corpo` | `const char * (const char *resposta)` | Localiza a linha em branco que separa o cabeçalho HTTP do corpo (HTML) da resposta. |
| `buscar_cabecalho` | `int (const char *resposta, const char *nome, char *destino, int tam_max)` | Procura um cabeçalho específico (ex.: `Location:`) e copia seu valor, usado para seguir redirecionamentos. |
| `traduzir_entidade` | `int (const char *texto, const char **saida)` | Converte entidades HTML (`&amp;`, `&aacute;`, etc.) para o caractere correspondente em UTF-8. |
| `renderizar_html` | `void (const char *html)` | Percorre o HTML caractere a caractere, descarta tags, scripts e estilos, e imprime apenas o texto visível. |
| `adicionar_historico` / `mostrar_historico` | `void (const char *url)` / `void (void)` | Mantêm uma lista das últimas páginas visitadas, usada pelas opções de histórico e "voltar". |
| `acessar` | `char * (const char *url_original, char *url_final)` | Função central que orquestra todo o fluxo: chama `analisar_url`, `baixar_pagina` e trata redirecionamentos em laço. |
| `main` | `int (void)` | Exibe o menu, lê a opção do usuário e chama as funções apropriadas até a opção "Sair" ser escolhida. |

## Suporte a HTTPS (TLS/SSL)

Sites modernos exigem conexões criptografadas por padrão. Para viabilizar esse suporte, o projeto utiliza a biblioteca **OpenSSL**, responsável por toda a complexidade criptográfica do protocolo TLS:

1. O socket TCP é aberto normalmente, na porta 443.
2. Um contexto SSL (`SSL_CTX`) e uma sessão SSL (`SSL`) são criados e associados a esse socket.
3. É configurado o **SNI** (Server Name Indication), que informa ao servidor qual domínio está sendo acessado — necessário porque um mesmo IP pode hospedar vários sites com certificados diferentes.
4. A função `SSL_connect()` realiza o handshake TLS: troca de certificados e negociação da chave de criptografia.
5. A partir daí, o envio e o recebimento de dados passam a usar `SSL_write()` e `SSL_read()` no lugar de `send()` e `recv()`, garantindo que o conteúdo trafegue criptografado.
6. Ao final, a sessão é encerrada com `SSL_shutdown()`, `SSL_free()` e `SSL_CTX_free()`, liberando a memória alocada pela biblioteca.

## Interpretação ("renderização") do HTML

Como o programa não possui interface gráfica, "renderizar" significa transformar o HTML bruto em texto legível. O algoritmo percorre o conteúdo caractere a caractere e:

- Ignora tudo entre `<` e `>` (as tags), pois são instruções de formatação, não conteúdo.
- Descarta o conteúdo de `<script>` e `<style>`, já que contêm código, não texto.
- Insere quebras de linha em tags de bloco (`<p>`, `<div>`, `<li>`, `<br>`, etc.) para manter a leitura organizada.
- Converte entidades HTML para o caractere correspondente (ex.: `&amp;` → `&`, `&aacute;` → `á`).
- Remove espaços e quebras de linha redundantes do HTML original.

## Testes realizados

**Servidor HTTP local:** foi criada uma página de teste com título, comentários, entidades acentuadas, listas e um bloco de script, servida localmente. O programa identificou corretamente o título, ignorou comentário e script, converteu as entidades e organizou o texto em parágrafos.

**HTTPS em sites reais:** foram testados acessos reais via `https://` (incluindo Google e PyPI). O log confirmou handshake bem-sucedido em TLS 1.3, download do conteúdo e exibição do texto renderizado corretamente.

**Limitação observada:** entidades HTML numéricas (`&#231;`) e algumas nomeadas menos comuns (`&copy;`) ainda aparecem sem conversão no texto final — está registrado como melhoria futura.

## Como compilar e executar

### Linux / macOS / WSL

```bash
sudo apt install libssl-dev   # cabeçalhos do OpenSSL, necessários para o HTTPS
gcc navegador.c -o navegador -lssl -lcrypto
./navegador
```

### Windows (MinGW)

```bash
gcc navegador.c -o navegador.exe -lssl -lcrypto -lws2_32
navegador.exe
```

## Conclusão

O desenvolvimento deste projeto permitiu aplicar, de forma prática, conceitos fundamentais de redes de computadores e programação em C que normalmente são vistos apenas na teoria: sockets, o protocolo HTTP, segurança com TLS e manipulação de memória dinâmica. Embora simplificado, o navegador reproduz fielmente as etapas centrais que qualquer navegador comercial executa antes de exibir uma página, evidenciando a camada de rede que costuma ficar invisível para o usuário final.

---

A versão formatada em Word (com capa e sumário) está disponível em [`Documentacao_Navegador_Web.docx`](./Documentacao_Navegador_Web.docx).
