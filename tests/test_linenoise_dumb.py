from pathlib import Path
import subprocess
import argparse
import tempfile

parser=argparse.ArgumentParser(description='Compare the pinned and build-patched linenoise basic-terminal readers.')
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--fixed-source', type=Path, required=True)
args=parser.parse_args()
source = args.source.read_text()
start = source.index('static int esp_linenoise_dumb(')
end = source.index('\nstatic void esp_linenoise_sanitize', start)
body = source[start:end]
prefix = r'''
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#define BACKSPACE 127
#define CTRL_H 8
#define UNIT_SEP 31
typedef struct {
 const char *prompt; int in_fd, out_fd;
 ssize_t (*read_bytes_cb)(int, void *, size_t);
 ssize_t (*write_bytes_cb)(int, const void *, size_t);
} esp_linenoise_config_t;
typedef struct { esp_linenoise_config_t config; } esp_linenoise_instance_t;
static const char *input;
static int read_calls;
static ssize_t rd(int fd, void *buf, size_t n) {
 (void)fd; (void)n; read_calls++;
 if (!*input) return -1;
 *(char *)buf = *input++; return 1;
}
static ssize_t wr(int fd, const void *buf, size_t n) {
 (void)fd; (void)buf; return n;
}
'''
suffix = r'''
int main(void) {
 esp_linenoise_instance_t inst = {.config={.prompt="> ", .read_bytes_cb=rd, .write_bytes_cb=wr}};
 unsigned char storage[258];
 memset(storage, 'Z', sizeof(storage)); storage[257] = 0;
 input = "print(1)\n";
 int n = esp_linenoise_dumb(&inst, (char *)storage, 256);
 int short_ok = n == 8 && !strcmp((char *)storage, "print(1)");
 printf("short command: count=%d strlen=%zu exact=%d\n", n, strlen((char *)storage), short_ok);
 char line[257]; memset(line, 'a', 255); line[255]='\n'; line[256]=0;
 memset(storage, 'Z', sizeof(storage)); storage[257]=0; input=line;
 n = esp_linenoise_dumb(&inst, (char *)storage, 256);
 int max_ok = n == 255 && storage[255] == 0 && storage[256] == 'Z';
 printf("maximum command: count=%d terminator=%u canary=%u safe=%d\n", n, storage[255], storage[256], max_ok);
 memset(storage, 'Z', sizeof(storage)); input="";
 n = esp_linenoise_dumb(&inst, (char *)storage, 256);
 int error_ok = n < 0 && storage[0] == 0 && storage[256] == 'Z';
 printf("read error: count=%d safe=%d\n", n, error_ok);
 return !(short_ok && max_ok && error_ok);
}
'''
patched=args.fixed_source.read_text()
start=patched.index('static int esp_linenoise_dumb(')
end=patched.index('\nstatic void esp_linenoise_sanitize',start)
fixed=patched[start:end]
assert fixed != body and 'count = nread;' not in fixed
with tempfile.TemporaryDirectory(prefix='linenoise-regression-') as work:
    for label, code in [('original', body), ('fixed', fixed)]:
        path = Path(work) / (label+'.c')
        path.write_text(prefix + code + suffix)
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(path.with_suffix(''))], check=True)
        result = subprocess.run([str(path.with_suffix(''))], text=True, capture_output=True, timeout=5)
        print(label + ':\n' + result.stdout)
        assert result.returncode == (1 if label == 'original' else 0)
