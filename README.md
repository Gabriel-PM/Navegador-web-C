# Navegador Web Simplificado em C

Trabalho da disciplina de Engenharia de Software — Curso de Engenharia de Software, UNIALFA (Centro Universitário Alves Faria).

**Autor:** Gabriel Pereira Martins

## Objetivo

O objetivo deste projeto é implementar, em linguagem C, um navegador web simplificado que roda em modo texto (linha de comando). A proposta é demonstrar na prática os conceitos que um navegador de verdade (Chrome, Firefox, Edge) utiliza por trás da interface gráfica:

- Comunicação em rede via **sockets TCP**
- Requisições usando o protocolo **HTTP/1.1**
- Conexões seguras via **HTTPS** (TLS), usando a biblioteca **OpenSSL**
- Tratamento de **redirecionamentos** (301, 302, 307, 308)
- Um interpretador simplificado de **HTML**, que remove tags e exibe apenas o texto visível ao usuário
- Gerenciamento de memória dinâmica (`malloc`/`realloc`) para lidar com páginas de tamanho variável

## Funcionalidades

- Abrir uma URL (HTTP ou HTTPS)
- Recarregar a página atual
- Ver histórico de navegação
- Voltar para a página anterior
- Ver o código-fonte HTML da página
- Salvar a página em um arquivo

## Como compilar

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

## Limitações

Por ser um trabalho acadêmico com escopo reduzido, o programa não implementa: renderização gráfica de layout, execução de JavaScript, aplicação de CSS, cookies, cache de páginas, múltiplas abas, download de arquivos binários (imagens, vídeos) e o protocolo HTTP/2. A tradução de entidades HTML cobre apenas os casos mais comuns.

## Documentação

A documentação técnica completa do projeto está disponível em [`Documentacao_Navegador_Web.docx`](./Documentacao_Navegador_Web.docx).
