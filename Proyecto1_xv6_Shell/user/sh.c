#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define MAXARGS  32
#define MAXLINE  512

/*
 * Proyecto 1 - xv6 Shell
 *
 * Shell de espacio de usuario para xv6-riscv.
 * Soporta:
 *   - comandos simples con argumentos
 *   - redireccion de entrada:  <
 *   - redireccion de salida: >
 *   - tuberias de una o varias etapas: |
 *
 * No implementa:
 *   - cd
 *   - ;, &, &&, ||
 *   - comillas
 *   - variables de entorno
 *   - 2>, 2>&1, >>
 *
 * La implementacion sigue las abstracciones de xv6:
 * fork + exec + wait
 * close + open para redireccion
 * pipe + dup + close para tuberias
 */

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

#define EXEC  1
#define REDIR 2
#define PIPE  3

static struct cmd *parsecmd(char *s);
static struct cmd *parsepipe(char **ps, char *es);
static struct cmd *parseexec(char **ps, char *es);
static struct cmd *parseredir(struct cmd *cmd, char **ps, char *es);
static void runcmd(struct cmd *cmd);

static int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;

  while(s < es && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n'))
    s++;

  if(s >= es){
    *ps = s;
    return 0;
  }

  ret = *s;

  if(ret == '<' || ret == '>' || ret == '|'){
    s++;
    *q = 0;
    *eq = 0;
    *ps = s;
    return ret;
  }

  *q = s;

  while(s < es &&
        *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n' &&
        *s != '<' && *s != '>' && *s != '|')
    s++;

  *eq = s;
  *ps = s;
  return 'a';
}

static int
peek(char **ps, char *es, char c)
{
  char *s = *ps;

  while(s < es && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n'))
    s++;

  *ps = s;
  return s < es && *s == c;
}

static struct cmd *
parseredir(struct cmd *cmd, char **ps, char *es)
{
  char *q, *eq;
  int tok;
  struct redircmd *rcmd;

  while(peek(ps, es, '<') || peek(ps, es, '>')){
    tok = gettoken(ps, es, 0, 0);

    if(gettoken(ps, es, &q, &eq) != 'a'){
      fprintf(2, "mish: falta el nombre del archivo despues de la redireccion\n");
      exit(1);
    }

    *eq = 0;

    rcmd = malloc(sizeof(*rcmd));
    memset(rcmd, 0, sizeof(*rcmd));
    rcmd->type = REDIR;
    rcmd->cmd = cmd;
    rcmd->file = q;

    if(tok == '<'){
      rcmd->mode = O_RDONLY;
      rcmd->fd = 0;
    } else {
      rcmd->mode = O_CREATE | O_WRONLY | O_TRUNC;
      rcmd->fd = 1;
    }

    cmd = (struct cmd *)rcmd;
  }

  return cmd;
}

static struct cmd *
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok;
  int argc = 0;
  struct execcmd *ecmd;
  struct cmd *ret;

  ecmd = malloc(sizeof(*ecmd));
  memset(ecmd, 0, sizeof(*ecmd));
  ecmd->type = EXEC;
  ret = (struct cmd *)ecmd;

  ret = parseredir(ret, ps, es);

  while(!peek(ps, es, '|')){
    tok = gettoken(ps, es, &q, &eq);

    if(tok == 0)
      break;

    if(tok == '<' || tok == '>'){
      /* gettoken consumed the symbol; process its filename here. */
      if(gettoken(ps, es, &q, &eq) != 'a'){
        fprintf(2, "mish: falta el nombre del archivo despues de la redireccion\n");
        exit(1);
      }

      *eq = 0;

      {
        struct redircmd *rcmd = malloc(sizeof(*rcmd));
        memset(rcmd, 0, sizeof(*rcmd));
        rcmd->type = REDIR;
        rcmd->cmd = ret;
        rcmd->file = q;

        if(tok == '<'){
          rcmd->mode = O_RDONLY;
          rcmd->fd = 0;
        } else {
          rcmd->mode = O_CREATE | O_WRONLY | O_TRUNC;
          rcmd->fd = 1;
        }

        ret = (struct cmd *)rcmd;
      }
      continue;
    }

    if(tok != 'a'){
      fprintf(2, "mish: sintaxis no valida\n");
      exit(1);
    }

    if(argc >= MAXARGS - 1){
      fprintf(2, "mish: demasiados argumentos (maximo %d)\n", MAXARGS - 1);
      exit(1);
    }

    *eq = 0;
    ecmd->argv[argc++] = q;

    /*
     * parseredir puede haber envuelto al EXEC en REDIR.
     * Los argumentos siempre pertenecen al EXEC original, por eso
     * ecmd conserva el arreglo argv mientras ret representa la envoltura.
     */
  }

  ecmd->argv[argc] = 0;

  if(argc == 0){
    fprintf(2, "mish: comando vacio\n");
    exit(1);
  }

  return ret;
}

static struct cmd *
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;
  struct pipecmd *pcmd;

  cmd = parseexec(ps, es);

  while(peek(ps, es, '|')){
    gettoken(ps, es, 0, 0);

    pcmd = malloc(sizeof(*pcmd));
    memset(pcmd, 0, sizeof(*pcmd));
    pcmd->type = PIPE;
    pcmd->left = cmd;
    pcmd->right = parsepipe(ps, es);
    cmd = (struct cmd *)pcmd;
  }

  return cmd;
}

static struct cmd *
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;
  char *p;

  p = s;
  es = s + strlen(s);

  cmd = parsepipe(&p, es);

  while(p < es && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
    p++;

  if(p != es){
    fprintf(2, "mish: sintaxis no valida cerca de '%c'\n", *p);
    exit(1);
  }

  return cmd;
}

static void
runcmd(struct cmd *cmd)
{
  int p[2];
  int pid1, pid2;
  int fd;
  struct execcmd *ecmd;
  struct redircmd *rcmd;
  struct pipecmd *pcmd;

  if(cmd == 0)
    exit(0);

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd *)cmd;

    if(ecmd->argv[0] == 0)
      exit(0);

    exec(ecmd->argv[0], ecmd->argv);

    fprintf(2, "mish: exec %s fallo\n", ecmd->argv[0]);
    exit(1);

  case REDIR:
    rcmd = (struct redircmd *)cmd;

    close(rcmd->fd);
    fd = open(rcmd->file, rcmd->mode);

    if(fd < 0){
      fprintf(2, "mish: no se pudo abrir %s\n", rcmd->file);
      exit(1);
    }

    if(fd != rcmd->fd){
      fprintf(2, "mish: error interno al configurar la redireccion\n");
      close(fd);
      exit(1);
    }

    runcmd(rcmd->cmd);
    exit(0);

  case PIPE:
    pcmd = (struct pipecmd *)cmd;

    if(pipe(p) < 0){
      fprintf(2, "mish: pipe fallo\n");
      exit(1);
    }

    pid1 = fork();
    if(pid1 < 0){
      fprintf(2, "mish: fork fallo\n");
      close(p[0]);
      close(p[1]);
      exit(1);
    }

    if(pid1 == 0){
      /* Hijo izquierdo: stdout -> pipe */
      close(1);
      if(dup(p[1]) < 0){
        fprintf(2, "mish: dup fallo\n");
        exit(1);
      }

      close(p[0]);
      close(p[1]);

      runcmd(pcmd->left);
      exit(0);
    }

    pid2 = fork();
    if(pid2 < 0){
      fprintf(2, "mish: fork fallo\n");
      close(p[0]);
      close(p[1]);
      wait(0);
      exit(1);
    }

    if(pid2 == 0){
      /* Hijo derecho: stdin <- pipe */
      close(0);
      if(dup(p[0]) < 0){
        fprintf(2, "mish: dup fallo\n");
        exit(1);
      }

      close(p[0]);
      close(p[1]);

      runcmd(pcmd->right);
      exit(0);
    }

    /*
     * El proceso que creo la etapa cierra ambos extremos.
     * Si deja abierto p[1], el lector podria no recibir EOF.
     */
    close(p[0]);
    close(p[1]);

    wait(0);
    wait(0);
    exit(0);

  default:
    fprintf(2, "mish: tipo de comando desconocido\n");
    exit(1);
  }
}

int
main(void)
{
  static char buf[MAXLINE];

  while(1){
    printf("mish> ");

    memset(buf, 0, sizeof(buf));

    if(gets(buf, sizeof(buf)) == 0)
      break;

    if(buf[0] == '\0' || buf[0] == '\n')
      continue;

    if(fork() == 0){
      struct cmd *cmd = parsecmd(buf);
      runcmd(cmd);
      exit(0);
    }

    wait(0);
  }

  exit(0);
}
