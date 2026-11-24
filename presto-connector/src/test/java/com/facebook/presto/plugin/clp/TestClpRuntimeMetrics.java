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
package com.facebook.presto.plugin.clp;

import static com.facebook.presto.testing.TestingSession.testSessionBuilder;
import static org.testng.Assert.assertEquals;
import static org.testng.Assert.assertFalse;
import static org.testng.Assert.assertNull;
import static org.testng.Assert.assertThrows;
import static org.testng.Assert.assertTrue;

import java.io.IOException;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

import com.facebook.presto.common.RuntimeStats;
import com.facebook.presto.spi.ConnectorSession;
import org.testng.annotations.Test;

public class TestClpRuntimeMetrics {
    private static final Path GLOSSARY = Paths.get("..", "docs", "runtime-metrics.md");

    @Test
    public void testEveryMetricIsInTheGlossary() throws IOException, IllegalAccessException {
        String glossary = Files.readString(GLOSSARY);
        int names = 0;
        for (Field field : ClpRuntimeMetrics.class.getFields()) {
            if (!Modifier.isStatic(field.getModifiers()) || String.class != field.getType()) {
                continue;
            }
            String name = (String)field.get(null);
            assertTrue(name.startsWith("clp"), name);
            assertTrue(glossary.contains("`" + name + "`"), name + " is not in " + GLOSSARY);
            ++names;
        }
        assertEquals(names, 21);
    }

    @Test
    public void testFlushesPendingValuesOnce() {
        ConnectorSession session = testSessionBuilder().build().toConnectorSession();
        ClpRuntimeMetrics.clearPending();
        assertFalse(ClpRuntimeMetrics.flushPending(session));

        ClpRuntimeMetrics.pendingNanos("clpTestNanos", 5);
        ClpRuntimeMetrics.pendingNanos("clpTestNanos", 7);
        assertTrue(ClpRuntimeMetrics.flushPending(session));
        assertFalse(ClpRuntimeMetrics.flushPending(session));

        RuntimeStats stats = session.getRuntimeStats();
        assertEquals(stats.getMetric("clpTestNanos").getSum(), 12);
        assertEquals(stats.getMetric("clpTestNanos").getCount(), 2);
    }

    @Test
    public void testClearPendingDropsValues() {
        ConnectorSession session = testSessionBuilder().build().toConnectorSession();
        ClpRuntimeMetrics.pendingNanos("clpTestNanos", 5);
        ClpRuntimeMetrics.clearPending();
        assertFalse(ClpRuntimeMetrics.flushPending(session));
        assertNull(session.getRuntimeStats().getMetric("clpTestNanos"));
    }

    @Test
    public void testTimesBodiesThatThrow() {
        ConnectorSession session = testSessionBuilder().build().toConnectorSession();
        assertThrows(
                IllegalStateException.class,
                () -> ClpRuntimeMetrics.time(session, "clpTestNanos", () -> {
                    throw new IllegalStateException();
                })
        );
        assertEquals(session.getRuntimeStats().getMetric("clpTestNanos").getCount(), 1);
    }

    @Test
    public void testIgnoresMissingSession() {
        ClpRuntimeMetrics.addNanos(null, "clpTestNanos", 1);
        assertEquals(ClpRuntimeMetrics.time(null, "clpTestNanos", () -> 3), 3);
    }
}
