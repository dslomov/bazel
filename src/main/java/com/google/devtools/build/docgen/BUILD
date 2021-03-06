load("@rules_java//java:defs.bzl", "java_binary", "java_library")

# Description:
#   Documentation generator for Bazel
package(
    default_visibility = ["//src:__subpackages__"],
)

java_library(
    name = "docgen_javalib",
    srcs = glob(["**/*.java"]),
    resources = [":template_files"],
    deps = [
        "//src/main/java/com/google/devtools/build/lib:build-base",
        "//src/main/java/com/google/devtools/build/lib:syntax",
        "//src/main/java/com/google/devtools/build/lib/cmdline",
        "//src/main/java/com/google/devtools/build/lib/collect/nestedset",
        "//src/main/java/com/google/devtools/build/lib/concurrent",
        "//src/main/java/com/google/devtools/build/lib/packages",
        "//src/main/java/com/google/devtools/build/lib/packages:build_type",
        "//src/main/java/com/google/devtools/build/lib/packages:type",
        "//src/main/java/com/google/devtools/build/lib/pkgcache",
        "//src/main/java/com/google/devtools/build/lib/skylarkbuildapi",
        "//src/main/java/com/google/devtools/build/lib/skylarkbuildapi/android",
        "//src/main/java/com/google/devtools/build/lib/skylarkbuildapi/apple",
        "//src/main/java/com/google/devtools/build/lib/skylarkbuildapi/config",
        "//src/main/java/com/google/devtools/build/lib/skylarkbuildapi/cpp",
        "//src/main/java/com/google/devtools/build/lib/skylarkbuildapi/java",
        "//src/main/java/com/google/devtools/build/lib/skylarkbuildapi/platform",
        "//src/main/java/com/google/devtools/build/lib/skylarkbuildapi/proto",
        "//src/main/java/com/google/devtools/build/lib/skylarkbuildapi/python",
        "//src/main/java/com/google/devtools/build/lib/skylarkbuildapi/repository",
        "//src/main/java/com/google/devtools/build/lib/skylarkbuildapi/stubs",
        "//src/main/java/com/google/devtools/build/lib/skylarkbuildapi/test",
        "//src/main/java/com/google/devtools/build/lib/skylarkinterface",
        "//src/main/java/com/google/devtools/build/lib/util",
        "//src/main/java/com/google/devtools/build/lib/util:classpath",
        "//src/main/java/com/google/devtools/build/lib/util:filetype",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/android",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/apple",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/config",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/cpp",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/java",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/platform",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/proto",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/python",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/repository",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/test",
        "//src/main/java/com/google/devtools/common/options",
        "//src/main/protobuf:builtin_java_proto",
        "//third_party:apache_velocity",
        "//third_party:guava",
        "//third_party:jsr305",
    ],
)

java_binary(
    name = "docgen_bin",
    srcs = ["BuildEncyclopediaGenerator.java"],
    main_class = "com.google.devtools.build.docgen.BuildEncyclopediaGenerator",
    runtime_deps = [
        "//src/main/java/com/google/devtools/build/lib:bazel-rules",
    ],
    deps = [
        ":docgen_javalib",
        "//src/main/java/com/google/devtools/build/lib:build-base",
        "//src/main/java/com/google/devtools/common/options",
    ],
)

java_binary(
    name = "skydoc_bin",
    srcs = ["SkylarkDocumentationGenerator.java"],
    main_class = "com.google.devtools.build.docgen.SkylarkDocumentationGenerator",
    runtime_deps = [
        "//src/main/java/com/google/devtools/build/lib:bazel-repository",
    ],
    deps = [":docgen_javalib"],
)

filegroup(
    name = "template_files",
    srcs = glob([
        "templates/**/*.html",
        "templates/**/*.vm",
    ]),
)

filegroup(
    name = "srcs",
    srcs = glob(["**"]) + [
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi:srcs",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/android:srcs",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/apple:srcs",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/config:srcs",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/cpp:srcs",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/java:srcs",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/platform:srcs",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/python:srcs",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/repository:srcs",
        "//src/main/java/com/google/devtools/build/skydoc/fakebuildapi/test:srcs",
        "//src/main/java/com/google/devtools/build/skydoc/rendering:srcs",
    ],
)
