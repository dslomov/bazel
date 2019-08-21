package com.google.devtools.build.lib.cmdline;

import com.google.common.collect.ImmutableMap;
import java.util.Map;

public class RepoMapping {
    private ImmutableMap<RepositoryName, RepositoryName> map;

    public RepoMapping(ImmutableMap<RepositoryName, RepositoryName> map) {
        this.map = map;
    }
    public static final RepoMapping EMPTY = new RepoMapping(ImmutableMap.of());


    public RepositoryName getOrDefault(RepositoryName from, RepositoryName defaultValue) {
        return map.getOrDefault(from, defaultValue);
    }

    public Iterable<? extends Map.Entry<RepositoryName, RepositoryName>> entrySet() {
        return map.entrySet();
    }
}
