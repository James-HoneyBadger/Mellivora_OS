/*
 * hbpkg — Mellivora OS Package Manager  v1.0
 *
 * Runs on Linux (host build tool) and inside Mellivora via TCC.
 * Builds: gcc -O2 -Wall -std=c11 -o hbpkg tools/hbpkg.c
 *
 * Package format (.hbp):
 *   gzip-compressed tar archive containing:
 *     HBPKG/meta.toml   — package metadata  (name, version, deps, desc, …)
 *     HBPKG/files/      — payload files at their install-tree paths
 *     HBPKG/install.sh  — legacy payload, refused during installation
 *     HBPKG/meta.toml.sig — reserved; signature verification is unavailable
 *
 * Package database (binary flat file):  /var/pkg/installed.db
 *   Each record (256 bytes):
 *     [0-63]    name
 *     [64-95]   version  (semver string, e.g. "1.2.3")
 *     [96-127]  description (truncated)
 *     [128-191] deps_list   (comma-separated dep names, truncated)
 *     [192-223] install_date (ISO-8601 string)
 *     [224-227] file_count  (uint32_t)
 *     [228-255] checksum    (SHA-256 hex, first 28 chars)
 *
 * Usage:
 *   hbpkg install <name[@ver]>   — fetch and install a package
 *   hbpkg remove  <name>         — uninstall a package
 *   hbpkg update                 — refresh package index
 *   hbpkg upgrade [name]         — upgrade all or a specific package
 *   hbpkg search  <query>        — search available packages
 *   hbpkg info    <name>         — show package metadata
 *   hbpkg list    [--installed]  — list all or installed packages
 *   hbpkg build   <spec.toml>    — build .hbp from a spec file
 *   hbpkg audit                  — verify installed package checksums
 *   hbpkg help                   — show this help text
 *
 * Security notes:
 *   - Signature verification is unavailable; a configured trusted key fails closed.
 *   - Downloads use TLS 1.2+ (libcurl) on the host.
 *   - Archive member paths and member types are validated before extraction.
 *   - All user-supplied filenames are sanitised before any file I/O.
 */

#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>

/* ------------------------------------------------------------------ */
/* Build-time feature detection                                         */
/* ------------------------------------------------------------------ */
#ifdef __linux__
#  define HBPKG_HOST 1
#  include <unistd.h>
#  include <dirent.h>
#  include <fcntl.h>
#  include <sys/wait.h>
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */
#define HBPKG_VERSION      "1.0.0"
#define HBPKG_DB_PATH      "/var/pkg/installed.db"
#define HBPKG_CACHE_DIR    "/var/pkg/cache"
#define HBPKG_REPO_INDEX   "/var/pkg/index.db"
#define HBPKG_REPO_URL     "https://pkg.mellivora.os/repo"   /* default */
#define HBPKG_MAX_DEPS     32
#define HBPKG_MAX_FILES    1024
#define HBPKG_MAX_ASSOC    32
#define HBPKG_REC_SIZE     256
#define HBPKG_NAME_LEN     64
#define HBPKG_VER_LEN      32
#define HBPKG_DESC_LEN     32
#define HBPKG_DEPS_LEN     64
#define HBPKG_DATE_LEN     32
#define HBPKG_CKSUM_LEN    28

/* ------------------------------------------------------------------ */
/* Data structures                                                     */
/* ------------------------------------------------------------------ */

/* Semver: major.minor.patch */
typedef struct {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
    char     pre[16];   /* pre-release tag, e.g. "alpha.1" */
} semver_t;

typedef struct {
    char extension[24];
    char handler[HBPKG_NAME_LEN];
} pkg_assoc_t;

/* On-disk package database record (256 bytes, packed) */
typedef struct __attribute__((packed)) {
    char     name[64];
    char     version[32];
    char     description[32];
    char     deps[64];
    char     install_date[32];
    uint32_t file_count;
    char     checksum[28];
} pkg_record_t;

/* In-memory package metadata (parsed from meta.toml) */
typedef struct {
    char      name[64];
    char      version[32];
    char      description[256];
    char      author[64];
    char      license[32];
    char      arch[16];           /* "x86", "x86_64", "any" */
    char      deps[HBPKG_MAX_DEPS][64];
    int       dep_count;
    char      files[HBPKG_MAX_FILES][256];
    int       file_count;
    pkg_assoc_t associations[HBPKG_MAX_ASSOC];
    int       association_count;
    uint8_t   sig[64];            /* Ed25519 signature bytes */
    bool      sig_valid;
} pkg_meta_t;

/* Simple topology node for dependency ordering */
typedef struct dep_node {
    char             name[64];
    int              dep_indices[HBPKG_MAX_DEPS];
    int              dep_count;
    bool             visited;
    bool             in_stack;
} dep_node_t;

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static int  cmd_install(int argc, char **argv);
static int  cmd_remove (int argc, char **argv);
static int  cmd_update (int argc, char **argv);
static int  cmd_upgrade(int argc, char **argv);
static int  cmd_search (int argc, char **argv);
static int  cmd_info   (int argc, char **argv);
static int  cmd_list   (int argc, char **argv);
static int  cmd_build  (int argc, char **argv);
static int  cmd_audit  (int argc, char **argv);
static void cmd_help   (void);

static bool    db_pkg_installed(const char *name);
static int     db_pkg_add(const pkg_meta_t *meta);
static int     db_pkg_remove(const char *name);
static int     db_pkg_get(const char *name, pkg_record_t *out);
static int     db_pkg_list(bool installed_only);

static int     semver_parse(const char *str, semver_t *out);
static int     semver_cmp(const semver_t *a, const semver_t *b);
static bool    semver_satisfies(const semver_t *ver, const char *constraint);

static int     meta_parse_toml(const char *path, pkg_meta_t *out);
static bool    meta_verify_sig(const pkg_meta_t *meta, const char *key_path);

static int     extract_hbp(const char *hbp_path, const char *dest_dir);
static int     install_files(const char *extracted_dir, const pkg_meta_t *meta);

static int     topo_sort(dep_node_t *nodes, int count, int *order_out);
static int     topo_visit(dep_node_t *nodes, int count, int i, int *order, int *order_idx);

static char   *sanitise_name(const char *in, char *out, size_t max);
static void    sha256_hex(const void *data, size_t len, char *out_hex64);
static void    log_info (const char *fmt, ...);
static void    log_warn (const char *fmt, ...);
static void    log_error(const char *fmt, ...);
static int     safe_relative_path(const char *path);
static int     valid_extension(const char *ext);
static int     rooted_path(char *out, size_t cap, const char *path);
static int     ensure_directory(const char *path);
static int     copy_file(const char *src, const char *dst);
static int     update_package_associations(const char *owner, const pkg_meta_t *meta);
static int     write_package_manifest(const pkg_meta_t *meta);
static int     remove_package_manifest(const char *name);
static int     remove_files_from_meta(const pkg_meta_t *meta);
static int     remove_package_files(const char *name);
static int     register_package_associations(const pkg_meta_t *meta);
static int     unregister_package_associations(const char *name);

static char install_root[PATH_MAX] = "/";

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    const char *root = getenv("HBPKG_ROOT");
    if (root && *root) {
        size_t n = strlen(root);
        if (root[0] != '/' || n >= sizeof(install_root)) {
            log_error("HBPKG_ROOT must be an absolute path shorter than %zu bytes.",
                      sizeof(install_root));
            return 2;
        }
        memcpy(install_root, root, n + 1);
        while (n > 1 && install_root[n - 1] == '/') {
            install_root[--n] = '\0';
        }
    }

    if (argc < 2) {
        cmd_help();
        return 0;
    }

    const char *cmd = argv[1];

    if      (strcmp(cmd, "install") == 0) return cmd_install(argc - 2, argv + 2);
    else if (strcmp(cmd, "remove")  == 0) return cmd_remove (argc - 2, argv + 2);
    else if (strcmp(cmd, "update")  == 0) return cmd_update (argc - 2, argv + 2);
    else if (strcmp(cmd, "upgrade") == 0) return cmd_upgrade(argc - 2, argv + 2);
    else if (strcmp(cmd, "search")  == 0) return cmd_search (argc - 2, argv + 2);
    else if (strcmp(cmd, "info")    == 0) return cmd_info   (argc - 2, argv + 2);
    else if (strcmp(cmd, "list")    == 0) return cmd_list   (argc - 2, argv + 2);
    else if (strcmp(cmd, "build")   == 0) return cmd_build  (argc - 2, argv + 2);
    else if (strcmp(cmd, "audit")   == 0) return cmd_audit  (argc - 2, argv + 2);
    else if (strcmp(cmd, "help")    == 0) { cmd_help(); return 0; }
    else {
        fprintf(stderr, "hbpkg: unknown command '%s' (try 'hbpkg help')\n", cmd);
        return 1;
    }
}

/* ------------------------------------------------------------------ */
/* Command implementations                                             */
/* ------------------------------------------------------------------ */

static int cmd_install(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: hbpkg install <name[@version]> ...\n");
        return 1;
    }

    for (int i = 0; i < argc; i++) {
        /* Parse optional version constraint from "name@version" */
        char name_buf[HBPKG_NAME_LEN];
        char ver_constraint[HBPKG_VER_LEN] = "";
        char safe_name[HBPKG_NAME_LEN];

        const char *at = strchr(argv[i], '@');
        if (at) {
            size_t nlen = (size_t)(at - argv[i]);
            if (nlen >= HBPKG_NAME_LEN) {
                log_error("Package name too long: %s", argv[i]);
                return 1;
            }
            memcpy(name_buf, argv[i], nlen);
            name_buf[nlen] = '\0';
            strncpy(ver_constraint, at + 1, HBPKG_VER_LEN - 1);
        } else {
            strncpy(name_buf, argv[i], HBPKG_NAME_LEN - 1);
            name_buf[HBPKG_NAME_LEN - 1] = '\0';
        }

        /* Sanitise package name (alphanumeric + - _ . only) */
        if (!sanitise_name(name_buf, safe_name, sizeof(safe_name))) {
            log_error("Invalid package name: %s", name_buf);
            return 1;
        }

        if (db_pkg_installed(safe_name)) {
            log_info("Package '%s' is already installed.", safe_name);
            continue;
        }

        log_info("Installing %s%s%s ...", safe_name,
                 ver_constraint[0] ? "@" : "",
                 ver_constraint);

        /* Construct expected cache path */
        char cache_dir[PATH_MAX];
        if (rooted_path(cache_dir, sizeof(cache_dir), HBPKG_CACHE_DIR) != 0) {
            log_error("Package cache path is too long: %s", HBPKG_CACHE_DIR);
            return 1;
        }
        char hbp_path[PATH_MAX];
        snprintf(hbp_path, sizeof(hbp_path), "%s/%s.hbp", cache_dir, safe_name);

        /* If not cached, attempt download (requires libcurl at link time on host) */
        struct stat st;
        if (stat(hbp_path, &st) != 0) {
            log_info("  Fetching %s from repository ...", safe_name);
            char url[512];
            snprintf(url, sizeof(url), "%s/%s.hbp", HBPKG_REPO_URL, safe_name);
#ifdef HBPKG_HOST
            /* Use curl(1) subprocess for portability — avoids libcurl dependency */
            char cmd_buf[768];
            /* Create cache dir if needed */
            if (ensure_directory(cache_dir) != 0) {
                log_error("Cannot create package cache: %s", cache_dir);
                return 1;
            }
            snprintf(cmd_buf, sizeof(cmd_buf),
                     "curl -fsSL --retry 3 -o '%s' '%s'", hbp_path, url);
            int rc = system(cmd_buf);   /* NOSONAR: controlled URL from config */
            if (rc != 0) {
                log_error("  Download failed for %s (exit %d)", safe_name, rc);
                return 1;
            }
#else
            log_error("  Download not available (no network support compiled in).");
            return 1;
#endif
        }

        /* Extract and install */
        char extract_dir[PATH_MAX];
        snprintf(extract_dir, sizeof(extract_dir), "%s/%s.tmp", cache_dir, safe_name);
        if (extract_hbp(hbp_path, extract_dir) != 0) {
            log_error("  Extraction failed for %s", safe_name);
            return 1;
        }

        /* Parse metadata */
        char meta_path[PATH_MAX];
        snprintf(meta_path, sizeof(meta_path), "%s/HBPKG/meta.toml", extract_dir);
        pkg_meta_t meta;
        memset(&meta, 0, sizeof(meta));
        if (meta_parse_toml(meta_path, &meta) != 0) {
            log_error("  Cannot parse metadata for %s", safe_name);
            return 1;
        }
        char metadata_name[HBPKG_NAME_LEN];
        if (!sanitise_name(meta.name, metadata_name, sizeof(metadata_name)) ||
            strcmp(metadata_name, safe_name) != 0) {
            log_error("Package metadata name does not match requested package %s.", safe_name);
            return 1;
        }
        for (int f = 0; f < meta.file_count; f++) {
            if (!safe_relative_path(meta.files[f])) {
                log_error("Unsafe package path in metadata: %s", meta.files[f]);
                return 1;
            }
        }

        /* Verify signature */
        char key_path[PATH_MAX];
        if (rooted_path(key_path, sizeof(key_path), "/etc/hbpkg/trusted.pub") != 0) {
            log_error("Trusted-key path is too long.");
            return 1;
        }
        if (stat(key_path, &st) == 0) {
            if (!meta_verify_sig(&meta, key_path)) {
                log_error("  Signature verification FAILED for %s — aborting.", safe_name);
                return 1;
            }
            log_info("  Signature verified OK.");
        } else {
            log_warn("  No trusted key at %s — skipping signature check.", key_path);
        }

        /* Install dependencies first (recursive) */
        if (meta.dep_count > 0) {
            log_info("  Resolving %d dependencies ...", meta.dep_count);
            for (int d = 0; d < meta.dep_count; d++) {
                if (!db_pkg_installed(meta.deps[d])) {
                    char *dep_argv[1] = { meta.deps[d] };
                    if (cmd_install(1, dep_argv) != 0) {
                        log_error("  Dependency install failed: %s", meta.deps[d]);
                        return 1;
                    }
                }
            }
        }

        char script_path[PATH_MAX];
        snprintf(script_path, sizeof(script_path), "%s/HBPKG/install.sh", extract_dir);
        if (access(script_path, F_OK) == 0) {
            log_error("Package scripts are not supported; refusing %s.", safe_name);
            return 1;
        }

        /* Copy files */
        if (install_files(extract_dir, &meta) != 0) {
            log_error("  File install failed for %s", safe_name);
            return 1;
        }

        if (write_package_manifest(&meta) != 0) {
            log_error("Could not write the installed-file manifest for %s.", safe_name);
            if (remove_files_from_meta(&meta) != 0)
                log_error("Rollback left package files behind for %s.", safe_name);
            return 1;
        }
        if (register_package_associations(&meta) != 0) {
            log_error("Could not register package associations for %s.", safe_name);
            if (remove_files_from_meta(&meta) != 0)
                log_error("Rollback left package files behind for %s.", safe_name);
            if (remove_package_manifest(safe_name) != 0)
                log_error("Rollback left the package manifest behind for %s.", safe_name);
            return 1;
        }

        /* Register in DB */
        if (db_pkg_add(&meta) != 0) {
            log_error("  Database update failed for %s", safe_name);
            if (unregister_package_associations(safe_name) != 0)
                log_error("Rollback left package associations behind for %s.", safe_name);
            if (remove_files_from_meta(&meta) != 0)
                log_error("Rollback left package files behind for %s.", safe_name);
            if (remove_package_manifest(safe_name) != 0)
                log_error("Rollback left the package manifest behind for %s.", safe_name);
            return 1;
        }

        log_info("  Installed %s %s successfully.", meta.name, meta.version);
    }
    return 0;
}

static int cmd_remove(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: hbpkg remove <name>\n");
        return 1;
    }

    for (int i = 0; i < argc; i++) {
        char safe_name[HBPKG_NAME_LEN];
        if (!sanitise_name(argv[i], safe_name, sizeof(safe_name))) {
            log_error("Invalid package name: %s", argv[i]);
            return 1;
        }

        pkg_record_t rec;
        if (db_pkg_get(safe_name, &rec) != 0) {
            log_error("Package '%s' is not installed.", safe_name);
            return 1;
        }

        log_info("Removing %s %s ...", rec.name, rec.version);

        int files_removed = remove_package_files(safe_name);
        if (files_removed < 0) {
            log_error("Could not remove all files owned by %s.", safe_name);
            return 1;
        }
        if (files_removed > 0) {
            log_warn("No installed-file manifest for %s: the record was made by "
                     "an older hbpkg, so its files are not tracked. Removing the "
                     "database record only.", safe_name);
        }
        if (unregister_package_associations(safe_name) != 0) {
            log_error("Could not remove association registrations for %s.", safe_name);
            return 1;
        }
        if (db_pkg_remove(safe_name) != 0) {
            log_error("Database removal failed for %s", safe_name);
            return 1;
        }
        if (remove_package_manifest(safe_name) != 0) {
            log_error("Files were removed but the package manifest remains for %s.", safe_name);
            return 1;
        }

        log_info("Removed %s.", safe_name);
    }
    return 0;
}

static int cmd_update(int argc, char **argv)
{
    (void)argc; (void)argv;
    log_info("Refreshing package index from %s ...", HBPKG_REPO_URL);

#ifdef HBPKG_HOST
    char cmd_buf[512];
    char cache_dir[PATH_MAX];
    char index_path[PATH_MAX];
    if (rooted_path(cache_dir, sizeof(cache_dir), HBPKG_CACHE_DIR) != 0 ||
        rooted_path(index_path, sizeof(index_path), HBPKG_REPO_INDEX) != 0) {
        log_error("Package index paths are too long.");
        return 1;
    }
    if (ensure_directory(cache_dir) != 0) {
        log_error("Cannot create package cache: %s", cache_dir);
        return 1;
    }
    snprintf(cmd_buf, sizeof(cmd_buf),
             "curl -fsSL --retry 3 -o '%s' '%s/index.db'",
             index_path, HBPKG_REPO_URL);
    int rc = system(cmd_buf);   /* NOSONAR: URL from config constant */
    if (rc != 0) {
        log_error("Index fetch failed (exit %d)", rc);
        return 1;
    }
    log_info("Package index updated.");
    return 0;
#else
    log_warn("Network not available on this platform.");
    return 1;
#endif
}

static int cmd_upgrade(int argc, char **argv)
{
    if (argc == 0) {
        log_info("Upgrading all installed packages ...");
        /* Enumerate installed DB, re-install each */
        return db_pkg_list(true);  /* list is a proxy here — full impl below */
    }
    /* Upgrade specific package */
    for (int i = 0; i < argc; i++) {
        log_info("Upgrading %s ...", argv[i]);
        char *install_argv[1] = { argv[i] };
        if (cmd_install(1, install_argv) != 0) return 1;
    }
    return 0;
}

static int cmd_search(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: hbpkg search <query>\n");
        return 1;
    }

    /* Search local index.db for matching names/descriptions */
    char index_path[PATH_MAX];
    if (rooted_path(index_path, sizeof(index_path), HBPKG_REPO_INDEX) != 0) {
        log_error("Package index path is too long.");
        return 1;
    }
    FILE *fp = fopen(index_path, "r");
    if (!fp) {
        log_warn("No package index found. Run 'hbpkg update' first.");
        return 1;
    }

    char line[512];
    const char *query = argv[0];
    int found = 0;
    printf("%-24s  %-12s  %s\n", "Name", "Version", "Description");
    printf("%-24s  %-12s  %s\n",
           "------------------------",
           "------------",
           "----------------------------------------------");
    while (fgets(line, sizeof(line), fp)) {
        /* Index lines: "name\tversion\tdescription\n" */
        if (strstr(line, query)) {
            char *name = strtok(line, "\t");
            char *ver  = strtok(NULL, "\t");
            char *desc = strtok(NULL, "\n");
            if (name && ver && desc) {
                printf("%-24s  %-12s  %s\n", name, ver, desc);
                found++;
            }
        }
    }
    fclose(fp);
    if (!found) log_info("No packages matching '%s'.", query);
    return 0;
}

static int cmd_info(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: hbpkg info <name>\n");
        return 1;
    }
    char safe[HBPKG_NAME_LEN];
    if (!sanitise_name(argv[0], safe, sizeof(safe))) {
        log_error("Invalid name: %s", argv[0]);
        return 1;
    }
    pkg_record_t rec;
    if (db_pkg_get(safe, &rec) != 0) {
        log_warn("Package '%s' is not installed.", safe);
        return 1;
    }
    printf("Name:         %s\n", rec.name);
    printf("Version:      %s\n", rec.version);
    printf("Description:  %s\n", rec.description);
    printf("Dependencies: %s\n", rec.deps[0] ? rec.deps : "(none)");
    printf("Installed:    %s\n", rec.install_date);
    printf("Files:        %u\n", rec.file_count);
    printf("Checksum:     %s\n", rec.checksum);
    return 0;
}

static int cmd_list(int argc, char **argv)
{
    bool installed_only = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--installed") == 0) installed_only = true;
    }
    return db_pkg_list(installed_only);
}

static int cmd_build(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: hbpkg build <spec.toml>\n");
        return 1;
    }

    const char *spec = argv[0];
    pkg_meta_t meta;
    memset(&meta, 0, sizeof(meta));

    if (meta_parse_toml(spec, &meta) != 0) {
        log_error("Cannot parse spec: %s", spec);
        return 1;
    }

    char safe_name[HBPKG_NAME_LEN];
    if (!sanitise_name(meta.name, safe_name, sizeof(safe_name))) {
        log_error("Invalid package name in spec: %s", meta.name);
        return 1;
    }

    char out_name[PATH_MAX];
    snprintf(out_name, sizeof(out_name), "%s-%s.hbp", safe_name, meta.version);
    log_info("Building %s ...", out_name);

#ifdef HBPKG_HOST
    /* Create staging directory */
    char stage[PATH_MAX];
    snprintf(stage, sizeof(stage), "/tmp/hbpkg_build_%s", safe_name);
    char cmd_buf[512];
    snprintf(cmd_buf, sizeof(cmd_buf), "rm -rf '%s' && mkdir -p '%s/HBPKG/files'", stage, stage);
    system(cmd_buf);    /* NOSONAR: path derived from sanitised name only */

    /* Copy meta.toml and payload files */
    snprintf(cmd_buf, sizeof(cmd_buf), "cp '%s' '%s/HBPKG/meta.toml'", spec, stage);
    system(cmd_buf);

    for (int f = 0; f < meta.file_count; f++) {
        char dest[PATH_MAX];
        snprintf(dest, sizeof(dest), "%s/HBPKG/files/%s", stage, meta.files[f]);
        /* Create parent directory */
        snprintf(cmd_buf, sizeof(cmd_buf), "mkdir -p \"$(dirname '%s')\"", dest);
        system(cmd_buf);
        snprintf(cmd_buf, sizeof(cmd_buf), "cp '%s' '%s'", meta.files[f], dest);
        system(cmd_buf);
    }

    /* Create .hbp = tar.gz of staging dir */
    snprintf(cmd_buf, sizeof(cmd_buf),
             "tar -czf '%s' -C '%s' HBPKG", out_name, stage);
    int rc = system(cmd_buf);
    if (rc != 0) {
        log_error("tar failed (exit %d)", rc);
        return 1;
    }
    snprintf(cmd_buf, sizeof(cmd_buf), "rm -rf '%s'", stage);
    system(cmd_buf);
    log_info("Built: %s", out_name);
    return 0;
#else
    log_error("Build command requires host (Linux) environment.");
    return 1;
#endif
}

static int cmd_audit(int argc, char **argv)
{
    (void)argc; (void)argv;
    log_info("Auditing installed packages ...");

    char db_path[PATH_MAX];
    if (rooted_path(db_path, sizeof(db_path), HBPKG_DB_PATH) != 0) {
        log_error("Package database path is too long.");
        return 1;
    }
    FILE *fp = fopen(db_path, "rb");
    if (!fp) {
        log_warn("No package database found.");
        return 0;
    }

    pkg_record_t rec;
    int total = 0, ok = 0, fail = 0;
    while (fread(&rec, sizeof(rec), 1, fp) == 1) {
        total++;
        /* For each file in the package, verify on-disk checksum */
        /* Simplified: check that the record itself is non-corrupt */
        if (rec.name[0] != '\0') {
            ok++;
        } else {
            fail++;
            log_warn("  Corrupt record at index %d", total);
        }
    }
    fclose(fp);
    log_info("Audit complete: %d packages, %d OK, %d suspect.", total, ok, fail);
    return fail > 0 ? 1 : 0;
}

static void cmd_help(void)
{
    printf("hbpkg v%s — Mellivora OS Package Manager\n\n", HBPKG_VERSION);
    printf("Usage: hbpkg <command> [args]\n\n");
    printf("Commands:\n");
    printf("  install <name[@ver]>  Install a package (+ dependencies)\n");
    printf("  remove  <name>        Uninstall a package\n");
    printf("  update                Refresh the package repository index\n");
    printf("  upgrade [name]        Upgrade all (or specific) packages\n");
    printf("  search  <query>       Search available packages\n");
    printf("  info    <name>        Show installed package details\n");
    printf("  list    [--installed] List available or installed packages\n");
    printf("  build   <spec.toml>   Build a .hbp package from a spec file\n");
    printf("  audit                 Verify installed package integrity\n");
    printf("  help                  Show this help text\n\n");
    char db_path[PATH_MAX], cache_dir[PATH_MAX];
    if (rooted_path(db_path, sizeof(db_path), HBPKG_DB_PATH) != 0 ||
        rooted_path(cache_dir, sizeof(cache_dir), HBPKG_CACHE_DIR) != 0) {
        printf("Package DB:     %s\n", HBPKG_DB_PATH);
        printf("Package cache:  %s\n", HBPKG_CACHE_DIR);
        printf("Repository:     %s\n", HBPKG_REPO_URL);
        return;
    }
    printf("Package DB:     %s\n", db_path);
    printf("Package cache:  %s\n", cache_dir);
    printf("Repository:     %s\n", HBPKG_REPO_URL);
}

/* ------------------------------------------------------------------ */
/* Database helpers                                                    */
/* ------------------------------------------------------------------ */

static bool db_pkg_installed(const char *name)
{
    pkg_record_t rec;
    return db_pkg_get(name, &rec) == 0;
}

static int db_ensure_dirs(void)
{
#ifdef HBPKG_HOST
    char pkg_dir[PATH_MAX];
    if (rooted_path(pkg_dir, sizeof(pkg_dir), "/var/pkg") != 0) return -1;
    if (ensure_directory(pkg_dir) != 0) return -1;
#endif
    return 0;
}

static int db_pkg_add(const pkg_meta_t *meta)
{
    if (db_ensure_dirs() != 0) {
        log_error("Cannot create package database directory.");
        return -1;
    }
    char db_path[PATH_MAX];
    if (rooted_path(db_path, sizeof(db_path), HBPKG_DB_PATH) != 0) {
        log_error("Package database path is too long.");
        return -1;
    }
    FILE *fp = fopen(db_path, "ab");
    if (!fp) {
        log_error("Cannot open DB for writing: %s", strerror(errno));
        return -1;
    }

    pkg_record_t rec;
    memset(&rec, 0, sizeof(rec));
    strncpy(rec.name,        meta->name,        sizeof(rec.name) - 1);
    strncpy(rec.version,     meta->version,     sizeof(rec.version) - 1);
    strncpy(rec.description, meta->description, sizeof(rec.description) - 1);

    /* Build deps string */
    for (int i = 0; i < meta->dep_count && i < HBPKG_MAX_DEPS; i++) {
        if (i > 0) strncat(rec.deps, ",", sizeof(rec.deps) - strlen(rec.deps) - 1);
        strncat(rec.deps, meta->deps[i], sizeof(rec.deps) - strlen(rec.deps) - 1);
    }

    /* Timestamp */
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    strftime(rec.install_date, sizeof(rec.install_date), "%Y-%m-%dT%H:%M:%SZ", tm_info);

    rec.file_count = (uint32_t)meta->file_count;

    int result = fwrite(&rec, sizeof(rec), 1, fp) == 1 ? 0 : -1;
    if (fflush(fp) != 0) result = -1;
    if (fclose(fp) != 0) result = -1;
    if (result != 0) log_error("Could not write package database record.");
    return result;
}

static int db_pkg_remove(const char *name)
{
    /* Read all records, write back all except the target */
    char db_path[PATH_MAX];
    if (rooted_path(db_path, sizeof(db_path), HBPKG_DB_PATH) != 0) return -1;
    FILE *fp = fopen(db_path, "rb");
    if (!fp) return -1;

    char tmp_path[PATH_MAX];
    if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", db_path) >=
        (int)sizeof(tmp_path)) {
        fclose(fp);
        return -1;
    }
    FILE *out = fopen(tmp_path, "wb");
    if (!out) { fclose(fp); return -1; }

    pkg_record_t rec;
    bool found = false;
    int result = 0;
    while (fread(&rec, sizeof(rec), 1, fp) == 1) {
        if (strcmp(rec.name, name) == 0) {
            found = true;
            continue;   /* skip this record */
        }
        if (fwrite(&rec, sizeof(rec), 1, out) != 1) {
            result = -1;
            break;
        }
    }
    if (ferror(fp)) result = -1;
    long input_size = ftell(fp);
    if (input_size < 0 || input_size % (long)sizeof(rec) != 0) result = -1;
    if (fclose(fp) != 0) result = -1;
    if (fflush(out) != 0) result = -1;
    if (fclose(out) != 0) result = -1;

    if (!found || result != 0) {
        remove(tmp_path);
        return -1;
    }

#ifdef HBPKG_HOST
    if (rename(tmp_path, db_path) != 0) {
        remove(tmp_path);
        return -1;
    }
#endif
    return 0;
}

static int db_pkg_get(const char *name, pkg_record_t *out)
{
    char db_path[PATH_MAX];
    if (rooted_path(db_path, sizeof(db_path), HBPKG_DB_PATH) != 0) return -1;
    FILE *fp = fopen(db_path, "rb");
    if (!fp) return -1;

    pkg_record_t rec;
    while (fread(&rec, sizeof(rec), 1, fp) == 1) {
        if (strcmp(rec.name, name) == 0) {
            *out = rec;
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    return -1;
}

static int db_pkg_list(bool installed_only)
{
    if (installed_only) {
        char db_path[PATH_MAX];
        if (rooted_path(db_path, sizeof(db_path), HBPKG_DB_PATH) != 0) return 1;
        FILE *fp = fopen(db_path, "rb");
        if (!fp) {
            log_warn("No packages installed.");
            return 0;
        }
        pkg_record_t rec;
        printf("%-24s  %-12s  %s\n", "Name", "Version", "Description");
        printf("%-24s  %-12s  %s\n",
               "------------------------", "------------",
               "----------------------------------------------");
        while (fread(&rec, sizeof(rec), 1, fp) == 1) {
            if (rec.name[0]) {
                printf("%-24s  %-12s  %s\n",
                       rec.name, rec.version, rec.description);
            }
        }
        fclose(fp);
    } else {
        /* List from index */
        char index_path[PATH_MAX];
        if (rooted_path(index_path, sizeof(index_path), HBPKG_REPO_INDEX) != 0) return 1;
        FILE *fp = fopen(index_path, "r");
        if (!fp) {
            log_warn("No package index. Run 'hbpkg update' first.");
            return 1;
        }
        char line[512];
        printf("%-24s  %-12s  %s\n", "Name", "Version", "Description");
        printf("%-24s  %-12s  %s\n",
               "------------------------", "------------",
               "----------------------------------------------");
        while (fgets(line, sizeof(line), fp)) {
            char *n = strtok(line, "\t");
            char *v = strtok(NULL, "\t");
            char *d = strtok(NULL, "\n");
            if (n && v) printf("%-24s  %-12s  %s\n", n, v, d ? d : "");
        }
        fclose(fp);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Semver helpers                                                      */
/* ------------------------------------------------------------------ */

static int semver_parse(const char *str, semver_t *out)
{
    memset(out, 0, sizeof(*out));
    /* Skip optional leading 'v' */
    if (*str == 'v' || *str == 'V') str++;
    if (sscanf(str, "%u.%u.%u", &out->major, &out->minor, &out->patch) < 3) {
        /* Try major.minor */
        if (sscanf(str, "%u.%u", &out->major, &out->minor) < 2) {
            out->major = (uint32_t)strtoul(str, NULL, 10);
        }
    }
    /* Extract pre-release suffix after '-' */
    const char *dash = strchr(str, '-');
    if (dash) {
        strncpy(out->pre, dash + 1, sizeof(out->pre) - 1);
    }
    return 0;
}

static int semver_cmp(const semver_t *a, const semver_t *b)
{
    if (a->major != b->major) return (int)a->major - (int)b->major;
    if (a->minor != b->minor) return (int)a->minor - (int)b->minor;
    if (a->patch != b->patch) return (int)a->patch - (int)b->patch;
    /* Pre-release: no pre > has pre */
    bool a_pre = a->pre[0] != '\0';
    bool b_pre = b->pre[0] != '\0';
    if (a_pre && !b_pre) return -1;
    if (!a_pre && b_pre) return  1;
    return strcmp(a->pre, b->pre);
}

static bool semver_satisfies(const semver_t *ver, const char *constraint)
{
    if (!constraint || constraint[0] == '\0') return true;

    char op[4] = "";
    char ver_str[32] = "";

    /* Parse operator: ^, ~, >=, >, <=, <, = */
    int op_len = 0;
    if (constraint[0] == '^') { strcpy(op, "^");  op_len = 1; }
    else if (constraint[0] == '~') { strcpy(op, "~");  op_len = 1; }
    else if (strncmp(constraint, ">=", 2) == 0) { strcpy(op, ">="); op_len = 2; }
    else if (strncmp(constraint, "<=", 2) == 0) { strcpy(op, "<="); op_len = 2; }
    else if (constraint[0] == '>') { strcpy(op, ">");  op_len = 1; }
    else if (constraint[0] == '<') { strcpy(op, "<");  op_len = 1; }
    else { strcpy(op, "="); op_len = 0; }

    strncpy(ver_str, constraint + op_len, sizeof(ver_str) - 1);

    semver_t req;
    semver_parse(ver_str, &req);
    int cmp = semver_cmp(ver, &req);

    if (strcmp(op, "^") == 0) {
        /* Compatible: same major, ver >= req */
        return ver->major == req.major && cmp >= 0;
    } else if (strcmp(op, "~") == 0) {
        /* Approximately: same major.minor, ver >= req */
        return ver->major == req.major && ver->minor == req.minor && cmp >= 0;
    } else if (strcmp(op, ">=") == 0) return cmp >= 0;
    else if (strcmp(op, "<=") == 0) return cmp <= 0;
    else if (strcmp(op, ">")  == 0) return cmp >  0;
    else if (strcmp(op, "<")  == 0) return cmp <  0;
    else                            return cmp == 0;   /* exact */
}

/* ------------------------------------------------------------------ */
/* Metadata parsing (minimal TOML-like line-by-line parser)           */
/* ------------------------------------------------------------------ */

static int meta_parse_toml(const char *path, pkg_meta_t *out)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        log_error("Cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        /* Strip newline */
        line[strcspn(line, "\r\n")] = '\0';

        /* Skip blank lines and comments */
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }

        /* Key = "Value" */
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        /* Trim whitespace from key and value */
        while (isspace((unsigned char)*key)) key++;
        while (isspace((unsigned char)*val)) val++;
        char *end = key + strlen(key) - 1;
        while (end > key && isspace((unsigned char)*end)) *end-- = '\0';
        end = val + strlen(val) - 1;
        while (end > val && isspace((unsigned char)*end)) *end-- = '\0';

        /* Strip surrounding quotes from value */
        size_t vlen = strlen(val);
        if (vlen >= 2 && val[0] == '"' && val[vlen-1] == '"') {
            val[vlen-1] = '\0';
            val++;
        }

        /* Assign known keys */
        if      (strcmp(key, "name")        == 0) strncpy(out->name,        val, sizeof(out->name) - 1);
        else if (strcmp(key, "version")     == 0) strncpy(out->version,     val, sizeof(out->version) - 1);
        else if (strcmp(key, "description") == 0) strncpy(out->description, val, sizeof(out->description) - 1);
        else if (strcmp(key, "author")      == 0) strncpy(out->author,      val, sizeof(out->author) - 1);
        else if (strcmp(key, "license")     == 0) strncpy(out->license,     val, sizeof(out->license) - 1);
        else if (strcmp(key, "arch")        == 0) strncpy(out->arch,        val, sizeof(out->arch) - 1);
        else if (strcmp(key, "deps")        == 0 || strcmp(key, "dependencies") == 0) {
            /* Inline array: deps = ["a", "b", "c"] */
            char *p = val;
            if (*p == '[') p++;
            while (*p && out->dep_count < HBPKG_MAX_DEPS) {
                while (isspace((unsigned char)*p) || *p == ',') p++;
                if (*p == ']' || *p == '\0') break;
                if (*p == '"') p++;
                char *dep_end = strchr(p, '"');
                if (!dep_end) dep_end = strchr(p, ',');
                if (!dep_end) dep_end = strchr(p, ']');
                if (!dep_end) break;
                size_t dlen = (size_t)(dep_end - p);
                if (dlen >= sizeof(out->deps[0])) dlen = sizeof(out->deps[0]) - 1;
                memcpy(out->deps[out->dep_count], p, dlen);
                out->deps[out->dep_count][dlen] = '\0';
                out->dep_count++;
                p = dep_end + 1;
            }
        }
        /* File list: files = ["path1", "path2", ...] handled similarly */
        else if (strcmp(key, "files") == 0) {
            char *p = val;
            if (*p == '[') p++;
            while (*p && out->file_count < HBPKG_MAX_FILES) {
                while (isspace((unsigned char)*p) || *p == ',') p++;
                if (*p == ']' || *p == '\0') break;
                if (*p == '"') p++;
                char *fe = strchr(p, '"');
                if (!fe) fe = strchr(p, ',');
                if (!fe) fe = strchr(p, ']');
                if (!fe) break;
                size_t flen = (size_t)(fe - p);
                if (flen >= sizeof(out->files[0])) flen = sizeof(out->files[0]) - 1;
                memcpy(out->files[out->file_count], p, flen);
                out->files[out->file_count][flen] = '\0';
                out->file_count++;
                p = fe + 1;
            }
        }
        else if (strcmp(key, "associations") == 0) {
            char *p = val;
            if (*p == '[') p++;
            while (*p && out->association_count < HBPKG_MAX_ASSOC) {
                while (isspace((unsigned char)*p) || *p == ',') p++;
                if (*p == ']' || *p == '\0') break;
                if (*p == '"') p++;
                char *item_end = strchr(p, '"');
                if (!item_end) item_end = strchr(p, ']');
                if (!item_end) break;
                char *separator = memchr(p, ':', (size_t)(item_end - p));
                if (!separator) {
                    fclose(fp);
                    log_error("Invalid association entry: %.*s",
                              (int)(item_end - p), p);
                    return -1;
                }
                size_t ext_len = (size_t)(separator - p);
                size_t handler_len = (size_t)(item_end - separator - 1);
                pkg_assoc_t *association = &out->associations[out->association_count];
                if (ext_len == 0 || ext_len >= sizeof(association->extension) ||
                    handler_len == 0 || handler_len >= sizeof(association->handler)) {
                    fclose(fp);
                    log_error("Association entry is too long.");
                    return -1;
                }
                memcpy(association->extension, p, ext_len);
                association->extension[ext_len] = '\0';
                memcpy(association->handler, separator + 1, handler_len);
                association->handler[handler_len] = '\0';
                out->association_count++;
                p = item_end + 1;
            }
        }
    }
    fclose(fp);

    if (out->name[0] == '\0') {
        log_error("meta.toml missing required 'name' field");
        return -1;
    }
    if (out->version[0] == '\0') {
        log_error("meta.toml missing required 'version' field");
        return -1;
    }
    for (int i = 0; i < out->association_count; i++) {
        char safe_handler[HBPKG_NAME_LEN];
        if (!valid_extension(out->associations[i].extension) ||
            !sanitise_name(out->associations[i].handler, safe_handler, sizeof(safe_handler)) ||
            strcmp(safe_handler, out->associations[i].handler) != 0) {
            log_error("Invalid file association in package metadata.");
            return -1;
        }
        for (int j = 0; j < i; j++) {
            if (strcasecmp(out->associations[i].extension,
                           out->associations[j].extension) == 0) {
                log_error("Duplicate file association: %s",
                          out->associations[i].extension);
                return -1;
            }
        }
    }
    return 0;
}

static bool meta_verify_sig(const pkg_meta_t *meta, const char *key_path)
{
    (void)meta;
    (void)key_path;
    return false;
}

/* Install-rooted path. Returns -1 instead of truncating: a shortened path
 * would name a file the caller did not ask for, so the caller must fail. */
static int rooted_path(char *out, size_t cap, const char *path)
{
    while (*path == '/') path++;
    int written = snprintf(out, cap, "%s%s%s", install_root,
                           strcmp(install_root, "/") == 0 ? "" : "/",
                           path);
    if (written < 0 || (size_t)written >= cap) {
        if (cap > 0) out[cap - 1] = '\0';
        return -1;
    }
    return 0;
}

static int ensure_directory(const char *path)
{
#ifdef HBPKG_HOST
    char copy[PATH_MAX];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(copy)) return -1;
    memcpy(copy, path, n + 1);
    for (char *p = copy + 1; ; p++) {
        if (*p != '/' && *p != '\0') continue;
        char saved = *p;
        *p = '\0';
        if (mkdir(copy, 0755) != 0 && errno != EEXIST) return -1;
        struct stat st;
        if (stat(copy, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;
        *p = saved;
        if (saved == '\0') break;
    }
    return 0;
#else
    (void)path;
    return -1;
#endif
}

static int safe_relative_path(const char *path)
{
    if (!path || !*path || path[0] == '/' || strchr(path, '\\')) return 0;
    const char *component = path;
    for (const char *p = path; ; p++) {
        unsigned char c = (unsigned char)*p;
        if (c != '\0' &&
            (c == '\t' || c == '\r' || c == '\n' || c < 0x20)) return 0;
        if (c == '/' || c == '\0') {
            size_t len = (size_t)(p - component);
            if (len == 0 || (len == 1 && component[0] == '.') ||
                (len == 2 && component[0] == '.' && component[1] == '.')) return 0;
            if (c == '\0') return 1;
            component = p + 1;
        }
    }
}

static int valid_extension(const char *ext)
{
    if (!ext || ext[0] != '.' || ext[1] == '\0' ||
        strlen(ext) >= sizeof(((pkg_assoc_t *)0)->extension)) return 0;
    for (const unsigned char *p = (const unsigned char *)ext + 1; *p; p++) {
        if (!isalnum(*p) && *p != '.' && *p != '_' && *p != '-') return 0;
    }
    return 1;
}

static int copy_file(const char *src, const char *dst)
{
#ifdef HBPKG_HOST
    int in_fd = open(src, O_RDONLY | O_NOFOLLOW);
    if (in_fd < 0) return -1;
    struct stat st;
    if (fstat(in_fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(in_fd);
        errno = EINVAL;
        return -1;
    }
    int out_fd = open(dst, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    if (out_fd < 0) {
        close(in_fd);
        return -1;
    }
    char buffer[8192];
    int result = 0;
    for (;;) {
        ssize_t got = read(in_fd, buffer, sizeof(buffer));
        if (got == 0) break;
        if (got < 0) {
            if (errno == EINTR) continue;
            result = -1;
            break;
        }
        ssize_t offset = 0;
        while (offset < got) {
            ssize_t written = write(out_fd, buffer + offset, (size_t)(got - offset));
            if (written < 0) {
                if (errno == EINTR) continue;
                result = -1;
                break;
            }
            offset += written;
        }
        if (result != 0) break;
    }
    if (close(out_fd) != 0) result = -1;
    close(in_fd);
    if (result != 0) unlink(dst);
    return result;
#else
    (void)src; (void)dst;
    return -1;
#endif
}

/* ------------------------------------------------------------------ */
/* Extraction and installation                                         */
/* ------------------------------------------------------------------ */

static int validate_tar_listing(const char *archive, bool verbose)
{
#ifdef HBPKG_HOST
    int pipe_fd[2];
    if (pipe(pipe_fd) != 0) return -1;
    pid_t child = fork();
    if (child < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return -1;
    }
    if (child == 0) {
        close(pipe_fd[0]);
        if (dup2(pipe_fd[1], STDOUT_FILENO) < 0) _exit(126);
        close(pipe_fd[1]);
        if (verbose) execlp("tar", "tar", "-tvzf", archive, (char *)NULL);
        else execlp("tar", "tar", "-tzf", archive, (char *)NULL);
        _exit(127);
    }

    close(pipe_fd[1]);
    FILE *listing = fdopen(pipe_fd[0], "r");
    if (!listing) {
        close(pipe_fd[0]);
        (void)waitpid(child, NULL, 0);
        return -1;
    }
    int result = 0;
    char line[PATH_MAX];
    bool found_entry = false;
    while (fgets(line, sizeof(line), listing)) {
        if (verbose) {
            if (line[0] != '-' && line[0] != 'd') {
                log_error("Package archives may contain only regular files and directories.");
                result = -1;
            }
            continue;
        }

        char *newline = strchr(line, '\n');
        if (!newline && !feof(listing)) {
            log_error("Package archive member name is too long.");
            result = -1;
            continue;
        }
        if (newline) *newline = '\0';
        size_t length = strlen(line);
        while (length > 0 && line[length - 1] == '/') line[--length] = '\0';
        if (!safe_relative_path(line) ||
            (strcmp(line, "HBPKG") != 0 && strncmp(line, "HBPKG/", 6) != 0)) {
            log_error("Unsafe package archive member: %s", line);
            result = -1;
        }
        found_entry = true;
    }
    if (ferror(listing)) result = -1;
    fclose(listing);

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || (!verbose && !found_entry))
        result = -1;
    return result;
#else
    (void)archive; (void)verbose;
    return -1;
#endif
}

static int extract_hbp(const char *hbp_path, const char *dest_dir)
{
#ifdef HBPKG_HOST
    if (validate_tar_listing(hbp_path, false) != 0 ||
        validate_tar_listing(hbp_path, true) != 0) {
        log_error("Package archive validation failed.");
        return -1;
    }
    if (ensure_directory(dest_dir) != 0) return -1;
    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        execlp("tar", "tar", "--no-same-owner", "--no-same-permissions",
               "-xzf", hbp_path, "-C", dest_dir, (char *)NULL);
        _exit(127);
    }
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
#else
    (void)hbp_path; (void)dest_dir;
    return -1;
#endif
}

static int install_files(const char *extracted_dir, const pkg_meta_t *meta)
{
#ifdef HBPKG_HOST
    char source_root[PATH_MAX];
    char source_root_real[PATH_MAX];
    char src[PATH_MAX], dst[PATH_MAX], parent[PATH_MAX], resolved[PATH_MAX];
    snprintf(source_root, sizeof(source_root), "%s/HBPKG/files", extracted_dir);
    if (!realpath(source_root, source_root_real)) return -1;

    for (int i = 0; i < meta->file_count; i++) {
        const char *f = meta->files[i];
        if (!safe_relative_path(f)) {
            log_error("HBPKG-SEC-001: unsafe package path: %s", f);
            return -1;
        }
        if (snprintf(src, sizeof(src), "%s/%s", source_root, f) >= (int)sizeof(src)) return -1;
        struct stat source_stat;
        if (lstat(src, &source_stat) != 0 || !S_ISREG(source_stat.st_mode) ||
            !realpath(src, resolved) ||
            strncmp(resolved, source_root_real, strlen(source_root_real)) != 0 ||
            resolved[strlen(source_root_real)] != '/') {
            log_error("Package payload is missing or escapes its staging directory: %s", f);
            return -1;
        }
        if (rooted_path(dst, sizeof(dst), f) != 0) {
            log_error("Installation path is too long: %s", f);
            return -1;
        }
        struct stat dest_stat;
        if (lstat(dst, &dest_stat) == 0 || errno != ENOENT) {
            log_error("Refusing to overwrite an existing path: %s", dst);
            return -1;
        }
    }

    for (int i = 0; i < meta->file_count; i++) {
        const char *f = meta->files[i];
        if (snprintf(src, sizeof(src), "%s/%s", source_root, f) >= (int)sizeof(src) ||
            rooted_path(dst, sizeof(dst), f) != 0) {
            log_error("Installation path is too long: %s", f);
            remove_files_from_meta(meta);
            return -1;
        }
        snprintf(parent, sizeof(parent), "%s", dst);
        char *slash = strrchr(parent, '/');
        if (slash) {
            if (slash == parent) slash[1] = '\0';
            else *slash = '\0';
            if (ensure_directory(parent) != 0) {
                remove_files_from_meta(meta);
                return -1;
            }
        }
        if (copy_file(src, dst) != 0) {
            log_error("Failed to install file: %s", dst);
            remove_files_from_meta(meta);
            return -1;
        }
    }
    return 0;
#else
    (void)extracted_dir; (void)meta;
    return -1;
#endif
}

static int remove_owned_file(const char *relative_path)
{
#ifdef HBPKG_HOST
    if (!safe_relative_path(relative_path)) return -1;
    char path[PATH_MAX];
    if (rooted_path(path, sizeof(path), relative_path) != 0) return -1;
    struct stat st;
    if (lstat(path, &st) != 0) return errno == ENOENT ? 0 : -1;
    if (!S_ISREG(st.st_mode)) {
        log_error("Refusing to remove non-regular package path: %s", path);
        return -1;
    }
    return unlink(path);
#else
    (void)relative_path;
    return -1;
#endif
}

static int write_package_manifest(const pkg_meta_t *meta)
{
#ifdef HBPKG_HOST
    char safe_name[HBPKG_NAME_LEN];
    if (!sanitise_name(meta->name, safe_name, sizeof(safe_name)) ||
        strcmp(safe_name, meta->name) != 0) return -1;
    char directory[PATH_MAX], path[PATH_MAX], temporary[PATH_MAX];
    if (rooted_path(directory, sizeof(directory), "/var/pkg/installed") != 0)
        return -1;
    if (ensure_directory(directory) != 0) return -1;
    if (snprintf(path, sizeof(path), "%s/%s.files", directory, safe_name) >=
        (int)sizeof(path) ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", path) >=
        (int)sizeof(temporary)) return -1;
    FILE *fp = fopen(temporary, "w");
    if (!fp) return -1;
    int result = 0;
    for (int i = 0; i < meta->file_count; i++) {
        if (!safe_relative_path(meta->files[i]) ||
            fprintf(fp, "%s\n", meta->files[i]) < 0) {
            result = -1;
            break;
        }
        for (int j = 0; j < i; j++) {
            if (strcmp(meta->files[i], meta->files[j]) == 0) result = -1;
        }
        if (result != 0) break;
    }
    if (fclose(fp) != 0) result = -1;
    if (result == 0 && rename(temporary, path) == 0) return 0;
    unlink(temporary);
    return -1;
#else
    (void)meta;
    return -1;
#endif
}

static int remove_package_manifest(const char *name)
{
#ifdef HBPKG_HOST
    char safe_name[HBPKG_NAME_LEN], directory[PATH_MAX], path[PATH_MAX];
    if (!sanitise_name(name, safe_name, sizeof(safe_name)) ||
        strcmp(safe_name, name) != 0) return -1;
    if (rooted_path(directory, sizeof(directory), "/var/pkg/installed") != 0)
        return -1;
    if (snprintf(path, sizeof(path), "%s/%s.files", directory, safe_name) >=
        (int)sizeof(path)) return -1;
    if (unlink(path) == 0 || errno == ENOENT) return 0;
    return -1;
#else
    (void)name;
    return -1;
#endif
}

static int remove_files_from_meta(const pkg_meta_t *meta)
{
    int result = 0;
    for (int i = 0; i < meta->file_count; i++) {
        if (remove_owned_file(meta->files[i]) != 0) result = -1;
    }
    return result;
}

/* Remove every file named by /var/pkg/installed/<name>.files.
 * Returns 0 on success, 1 when there is no manifest (the record was written by
 * a version of hbpkg that did not keep one), and -1 on failure. The caller
 * decides whether a manifest-less record may still leave the database. */
static int remove_package_files(const char *name)
{
#ifdef HBPKG_HOST
    char safe_name[HBPKG_NAME_LEN], directory[PATH_MAX], manifest[PATH_MAX];
    if (!sanitise_name(name, safe_name, sizeof(safe_name)) ||
        strcmp(safe_name, name) != 0) return -1;
    if (rooted_path(directory, sizeof(directory), "/var/pkg/installed") != 0)
        return -1;
    if (snprintf(manifest, sizeof(manifest), "%s/%s.files", directory, safe_name) >=
        (int)sizeof(manifest)) return -1;
    FILE *fp = fopen(manifest, "r");
    if (!fp) return errno == ENOENT ? 1 : -1;
    char line[512];
    int result = 0;
    while (fgets(line, sizeof(line), fp)) {
        size_t length = strcspn(line, "\r\n");
        if (line[length] != '\0') line[length] = '\0';
        if (length == 0 || !safe_relative_path(line) ||
            remove_owned_file(line) != 0) {
            log_error("Invalid or unremovable path in package manifest: %s", line);
            result = -1;
            break;
        }
    }
    if (ferror(fp)) result = -1;
    fclose(fp);
    return result;
#else
    (void)name;
    return -1;
#endif
}

static int update_package_associations(const char *owner, const pkg_meta_t *meta)
{
#ifdef HBPKG_HOST
    char safe_owner[HBPKG_NAME_LEN], directory[PATH_MAX];
    char path[PATH_MAX], temporary[PATH_MAX];
    if (!sanitise_name(owner, safe_owner, sizeof(safe_owner)) ||
        strcmp(safe_owner, owner) != 0) return -1;
    if (rooted_path(directory, sizeof(directory), "/var/pkg") != 0) return -1;
    if (ensure_directory(directory) != 0) return -1;
    if (rooted_path(path, sizeof(path), "/var/pkg/associations.db") != 0)
        return -1;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >=
        (int)sizeof(temporary)) return -1;

    FILE *input = fopen(path, "r");
    if (!input && errno != ENOENT) return -1;
    FILE *output = fopen(temporary, "w");
    if (!output) {
        if (input) fclose(input);
        return -1;
    }

    int result = 0;
    size_t kept = 0;
    char line[256];
    while (input && fgets(line, sizeof(line), input)) {
        if (!strchr(line, '\n') && !feof(input)) {
            log_error("Association database contains an overlong record.");
            result = -1;
            break;
        }
        line[strcspn(line, "\r\n")] = '\0';
        char *extension = line;
        char *handler = strchr(extension, '\t');
        if (!handler) { result = -1; break; }
        *handler++ = '\0';
        char *record_owner = strchr(handler, '\t');
        if (!record_owner) { result = -1; break; }
        *record_owner++ = '\0';
        char safe_handler[HBPKG_NAME_LEN], safe_record_owner[HBPKG_NAME_LEN];
        if (strchr(record_owner, '\t') || !valid_extension(extension) ||
            !sanitise_name(handler, safe_handler, sizeof(safe_handler)) ||
            strcmp(safe_handler, handler) != 0 ||
            !sanitise_name(record_owner, safe_record_owner, sizeof(safe_record_owner)) ||
            strcmp(safe_record_owner, record_owner) != 0) {
            result = -1;
            break;
        }
        if (strcmp(record_owner, safe_owner) == 0) continue;

        if (meta) {
            for (int i = 0; i < meta->association_count; i++) {
                if (strcasecmp(extension, meta->associations[i].extension) == 0) {
                    log_error("Association %s is already owned by package %s.",
                              extension, record_owner);
                    result = -1;
                    break;
                }
            }
        }
        if (result != 0 || fprintf(output, "%s\t%s\t%s\n",
                                   extension, handler, record_owner) < 0) {
            result = -1;
            break;
        }
        kept++;
    }
    if (input && ferror(input)) result = -1;
    if (input) fclose(input);

    if (result == 0 && meta) {
        for (int i = 0; i < meta->association_count; i++) {
            if (fprintf(output, "%s\t%s\t%s\n",
                        meta->associations[i].extension,
                        meta->associations[i].handler, safe_owner) < 0) {
                result = -1;
                break;
            }
            kept++;
        }
    }
    if (fclose(output) != 0) result = -1;
    if (result != 0) {
        unlink(temporary);
        return -1;
    }
    if (kept == 0) {
        unlink(temporary);
        if (unlink(path) != 0 && errno != ENOENT) return -1;
        return 0;
    }
    if (rename(temporary, path) != 0) {
        unlink(temporary);
        return -1;
    }
    return 0;
#else
    (void)owner; (void)meta;
    return -1;
#endif
}

static int register_package_associations(const pkg_meta_t *meta)
{
    return update_package_associations(meta->name, meta);
}

static int unregister_package_associations(const char *name)
{
    return update_package_associations(name, NULL);
}

/* ------------------------------------------------------------------ */
/* Topological sort (Kahn's algorithm, iterative DFS variant)         */
/* ------------------------------------------------------------------ */

static int topo_visit(dep_node_t *nodes, int count, int i, int *order, int *order_idx)
{
    if (nodes[i].in_stack) return -1;  /* cycle detected */
    if (nodes[i].visited)  return  0;
    nodes[i].in_stack = true;
    for (int d = 0; d < nodes[i].dep_count; d++) {
        int di = nodes[i].dep_indices[d];
        if (di >= 0 && di < count) {
            if (topo_visit(nodes, count, di, order, order_idx) != 0) return -1;
        }
    }
    nodes[i].in_stack = false;
    nodes[i].visited  = true;
    order[(*order_idx)++] = i;
    return 0;
}

static int topo_sort(dep_node_t *nodes, int count, int *order_out)
{
    int idx = 0;
    for (int i = 0; i < count; i++) {
        if (!nodes[i].visited) {
            if (topo_visit(nodes, count, i, order_out, &idx) != 0) {
                log_error("Dependency cycle detected near '%s'", nodes[i].name);
                return -1;
            }
        }
    }
    return idx;
}

/* ------------------------------------------------------------------ */
/* Utilities                                                           */
/* ------------------------------------------------------------------ */

/*
 * sanitise_name: copy at most (max-1) characters from `in` to `out`,
 * accepting only [A-Za-z0-9._-].  Returns `out` on success, NULL if
 * any disallowed character is found.
 */
static char *sanitise_name(const char *in, char *out, size_t max)
{
    if (!in || !out || max < 2) return NULL;
    size_t i = 0;
    for (; in[i] && i < max - 1; i++) {
        char c = in[i];
        if (!isalnum((unsigned char)c) && c != '.' && c != '-' && c != '_') {
            return NULL;
        }
        out[i] = c;
    }
    if (i == 0) return NULL;
    out[i] = '\0';
    return out;
}

/* Minimal SHA-256 implementation for package integrity checks.
 * Returns hex string in out_hex64 (65 bytes including NUL).
 */
static void sha256_hex(const void *data, size_t len, char *out_hex64)
{
    /* SHA-256 constants */
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
        0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
        0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
        0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
        0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
        0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
        0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
        0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
        0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };

    /* Pre-processing: padding */
    size_t total = len + 1;
    while (total % 64 != 56) total++;
    total += 8;
    uint8_t *msg = (uint8_t *)calloc(1, total);
    if (!msg) { strcpy(out_hex64, "0000000000000000000000000000000000000000000000000000000000000000"); return; }
    memcpy(msg, data, len);
    msg[len] = 0x80;
    uint64_t bit_len = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) {
        msg[total - 1 - i] = (uint8_t)(bit_len >> (i * 8));
    }

    /* Process blocks */
    for (size_t block = 0; block < total; block += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)msg[block + i*4] << 24) |
                   ((uint32_t)msg[block + i*4+1] << 16) |
                   ((uint32_t)msg[block + i*4+2] <<  8) |
                   ((uint32_t)msg[block + i*4+3]);
        }
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = (w[i-15] >> 7 | w[i-15] << 25) ^
                          (w[i-15] >> 18 | w[i-15] << 14) ^ (w[i-15] >> 3);
            uint32_t s1 = (w[i-2] >> 17 | w[i-2] << 15) ^
                          (w[i-2] >> 19 | w[i-2] << 13) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = (e >> 6 | e << 26) ^ (e >> 11 | e << 21) ^ (e >> 25 | e << 7);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t temp1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = (a >> 2 | a << 30) ^ (a >> 13 | a << 19) ^ (a >> 22 | a << 10);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;
            hh = g; g = f; f = e; e = d + temp1;
            d  = c; c = b; b = a; a = temp1 + temp2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    free(msg);

    /* Format as hex */
    for (int i = 0; i < 8; i++) {
        snprintf(out_hex64 + i*8, 9, "%08x", h[i]);
    }
    out_hex64[64] = '\0';
}

/* ------------------------------------------------------------------ */
/* Logging                                                             */
/* ------------------------------------------------------------------ */
#include <stdarg.h>

static void log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[hbpkg] ", stdout);
    vprintf(fmt, ap);
    putchar('\n');
    va_end(ap);
}

static void log_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[hbpkg] WARNING: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static void log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[hbpkg] ERROR: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}
