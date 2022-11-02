from os.path import join, isfile

Import("env")

FRAMEWORK_DIR = join (".pio", "libdeps", "ttgo-t-watch", "TTGO TWatch Library")
patchflag_path = join(FRAMEWORK_DIR, ".patching-done")

# patch file only if we didn't do it before
if not isfile(join(FRAMEWORK_DIR, ".patching-done")):
    original_file = join(FRAMEWORK_DIR, "src", "lv_conf.h")
    patched_file = join("patches", "1-ttgo-t-watch-select-fonts.patch")

    assert isfile(original_file) and isfile(patched_file)

    env.Execute("patch \"%s\" \"%s\"" % (original_file, patched_file))
    # env.Execute("touch " + patchflag_path)


    def _touch(path):
        with open(path, "w") as fp:
            fp.write("")

    env.Execute(lambda *args, **kwargs: _touch(patchflag_path))