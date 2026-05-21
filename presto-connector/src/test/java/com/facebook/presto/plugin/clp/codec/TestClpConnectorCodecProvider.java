/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.facebook.presto.plugin.clp.codec;

import com.facebook.presto.common.type.BigintType;
import com.facebook.presto.common.type.IntegerType;
import com.facebook.presto.common.type.Type;
import com.facebook.presto.common.type.TypeManager;
import com.facebook.presto.common.type.TypeSignature;
import com.facebook.presto.common.type.TypeSignatureParameter;
import com.facebook.presto.common.type.VarcharType;
import com.facebook.presto.plugin.clp.ClpColumnHandle;
import com.facebook.presto.plugin.clp.ClpSplit;
import com.facebook.presto.plugin.clp.ClpTableHandle;
import com.facebook.presto.plugin.clp.ClpTableLayoutHandle;
import com.facebook.presto.plugin.clp.ClpTransactionHandle;
import com.facebook.presto.spi.SchemaTableName;
import com.facebook.presto.spi.connector.ConnectorTransactionHandle;
import org.testng.annotations.Test;

import java.util.List;
import java.util.Optional;

import static org.testng.Assert.assertEquals;

public class TestClpConnectorCodecProvider
{
    private static final TypeManager TYPE_MANAGER = new TypeManager()
    {
        @Override
        public Type getType(TypeSignature signature)
        {
            switch (signature.getBase()) {
                case "varchar":
                    return VarcharType.createUnboundedVarcharType();
                case "bigint":
                    return BigintType.BIGINT;
                case "integer":
                    return IntegerType.INTEGER;
                default:
                    throw new IllegalArgumentException("Unknown type: " + signature);
            }
        }

        @Override
        public Type getParameterizedType(String baseTypeName, List<TypeSignatureParameter> typeParameters)
        {
            return getType(new TypeSignature(baseTypeName));
        }

        @Override
        public boolean canCoerce(Type actualType, Type expectedType)
        {
            throw new UnsupportedOperationException();
        }

        @Override
        public List<Type> getTypes()
        {
            throw new UnsupportedOperationException();
        }

        @Override
        public boolean hasType(TypeSignature signature)
        {
            return getType(signature) != null;
        }
    };

    @Test
    public void testColumnHandleRoundtrip()
    {
        ClpColumnHandleCodec codec = new ClpColumnHandleCodec(TYPE_MANAGER);
        ClpColumnHandle original = new ClpColumnHandle("ts", "timestamp", BigintType.BIGINT);
        byte[] bytes = codec.serialize(original);
        ClpColumnHandle deserialized = (ClpColumnHandle) codec.deserialize(bytes);

        assertEquals(deserialized.getColumnName(), "ts");
        assertEquals(deserialized.getOriginalColumnName(), "timestamp");
        assertEquals(deserialized.getColumnType(), BigintType.BIGINT);
    }

    @Test
    public void testSplitRoundtripArchiveWithKql()
    {
        ClpSplitCodec codec = new ClpSplitCodec();
        ClpSplit original = new ClpSplit("/data/logs/archive.clp", ClpSplit.SplitType.ARCHIVE, Optional.of("LEVEL:ERROR"));
        byte[] bytes = codec.serialize(original);
        ClpSplit deserialized = (ClpSplit) codec.deserialize(bytes);

        assertEquals(deserialized.getPath(), "/data/logs/archive.clp");
        assertEquals(deserialized.getType(), ClpSplit.SplitType.ARCHIVE);
        assertEquals(deserialized.getKqlQuery(), Optional.of("LEVEL:ERROR"));
    }

    @Test
    public void testSplitRoundtripIrNoKql()
    {
        ClpSplitCodec codec = new ClpSplitCodec();
        ClpSplit original = new ClpSplit("/data/ir/stream.clp.zst", ClpSplit.SplitType.IR, Optional.empty());
        byte[] bytes = codec.serialize(original);
        ClpSplit deserialized = (ClpSplit) codec.deserialize(bytes);

        assertEquals(deserialized.getPath(), "/data/ir/stream.clp.zst");
        assertEquals(deserialized.getType(), ClpSplit.SplitType.IR);
        assertEquals(deserialized.getKqlQuery(), Optional.empty());
    }

    @Test
    public void testTableHandleRoundtrip()
    {
        ClpTableHandleCodec codec = new ClpTableHandleCodec();
        ClpTableHandle original = new ClpTableHandle(new SchemaTableName("default", "logs"), "/data/logs");
        byte[] bytes = codec.serialize(original);
        ClpTableHandle deserialized = (ClpTableHandle) codec.deserialize(bytes);

        assertEquals(deserialized.getSchemaTableName().getSchemaName(), "default");
        assertEquals(deserialized.getSchemaTableName().getTableName(), "logs");
        assertEquals(deserialized.getTablePath(), "/data/logs");
    }

    @Test
    public void testTableLayoutHandleRoundtripWithKql()
    {
        ClpTableLayoutHandleCodec codec = new ClpTableLayoutHandleCodec();
        ClpTableHandle table = new ClpTableHandle(new SchemaTableName("clp", "logs"), "/data/logs");
        ClpTableLayoutHandle original = new ClpTableLayoutHandle(table, Optional.of("level:ERROR"), Optional.empty());
        byte[] bytes = codec.serialize(original);
        ClpTableLayoutHandle deserialized = (ClpTableLayoutHandle) codec.deserialize(bytes);

        assertEquals(deserialized.getTable().getSchemaTableName().getSchemaName(), "clp");
        assertEquals(deserialized.getTable().getSchemaTableName().getTableName(), "logs");
        assertEquals(deserialized.getTable().getTablePath(), "/data/logs");
        assertEquals(deserialized.getKqlQuery(), Optional.of("level:ERROR"));
        assertEquals(deserialized.getMetadataSql(), Optional.empty());
    }

    @Test
    public void testTableLayoutHandleRoundtripWithMetadataSql()
    {
        ClpTableLayoutHandleCodec codec = new ClpTableLayoutHandleCodec();
        ClpTableHandle table = new ClpTableHandle(new SchemaTableName("clp", "logs"), "/data");
        ClpTableLayoutHandle original = new ClpTableLayoutHandle(table, Optional.empty(), Optional.of("(end_timestamp > 100)"));
        byte[] bytes = codec.serialize(original);
        ClpTableLayoutHandle deserialized = (ClpTableLayoutHandle) codec.deserialize(bytes);

        assertEquals(deserialized.getTable().getSchemaTableName().getSchemaName(), "clp");
        assertEquals(deserialized.getKqlQuery(), Optional.empty());
        assertEquals(deserialized.getMetadataSql(), Optional.of("(end_timestamp > 100)"));
    }

    @Test
    public void testTransactionHandleRoundtrip()
    {
        ClpTransactionHandleCodec codec = new ClpTransactionHandleCodec();
        byte[] bytes = codec.serialize(ClpTransactionHandle.INSTANCE);
        assertEquals(bytes.length, 0);

        ConnectorTransactionHandle deserialized = codec.deserialize(bytes);
        assertEquals(deserialized, ClpTransactionHandle.INSTANCE);
    }
}
