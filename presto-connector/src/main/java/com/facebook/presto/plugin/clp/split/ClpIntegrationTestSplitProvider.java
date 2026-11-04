/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.facebook.presto.plugin.clp.split;

import static com.facebook.presto.plugin.clp.ClpErrorCode.CLP_INTEGRATION_TEST_FIXTURE_INVALID;
import static com.facebook.presto.plugin.clp.ClpSplit.SplitType.ARCHIVE;
import static com.facebook.presto.plugin.clp.ClpSplit.SplitType.IR;
import static java.lang.String.format;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;
import java.util.stream.Stream;

import com.facebook.presto.spi.PrestoException;
import com.google.common.collect.ImmutableList;

import com.facebook.presto.plugin.clp.ClpSplit;
import com.facebook.presto.plugin.clp.ClpTableLayoutHandle;

/**
 * Discovers archives from a table's directory on the local filesystem rather than a metadata
 * database, one split per archive.
 */
public class ClpIntegrationTestSplitProvider implements ClpSplitProvider {
    private static final String ARCHIVE_SUFFIX = ".clps";
    private static final String IR_SUFFIX = ".clp.zst";

    @Override
    public List<ClpSplit> listSplits(ClpTableLayoutHandle clpTableLayoutHandle) {
        // Handed to the worker as-is, so the archive directory must be mounted at the same path on
        // the coordinator and the workers.
        Path tablePath = Paths.get(clpTableLayoutHandle.getTable().getTablePath());

        ImmutableList.Builder<ClpSplit> splits = ImmutableList.builder();
        try (Stream<Path> entries = Files.list(tablePath)) {
            entries.filter(ClpIntegrationTestSplitProvider::hasSplitSuffix).sorted().forEach(
                    entry -> splits.add(
                            new ClpSplit(
                                    entry.toString(),
                                    entry.getFileName().toString().endsWith(IR_SUFFIX) ? IR
                                            : ARCHIVE,
                                    clpTableLayoutHandle.getKqlQuery()
                            )
                    )
            );
        } catch (IOException e) {
            throw new PrestoException(
                    CLP_INTEGRATION_TEST_FIXTURE_INVALID,
                    format("Failed to list %s", tablePath),
                    e
            );
        }
        return splits.build();
    }

    // A table's directory may also hold a schema.json, which is neither.
    private static boolean hasSplitSuffix(Path entry) {
        String name = entry.getFileName().toString();
        return name.endsWith(ARCHIVE_SUFFIX) || name.endsWith(IR_SUFFIX);
    }
}
