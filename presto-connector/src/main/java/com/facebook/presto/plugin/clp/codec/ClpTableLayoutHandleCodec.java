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

import com.facebook.presto.plugin.clp.ClpTableHandle;
import com.facebook.presto.plugin.clp.ClpTableLayoutHandle;
import com.facebook.presto.spi.ConnectorCodec;
import com.facebook.presto.spi.ConnectorTableLayoutHandle;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.util.Optional;

import static com.facebook.presto.plugin.clp.codec.ClpTableHandleCodec.readTableHandle;
import static com.facebook.presto.plugin.clp.codec.ClpTableHandleCodec.writeTableHandle;
import static com.facebook.presto.plugin.clp.codec.CodecUtils.readUtf8String;
import static com.facebook.presto.plugin.clp.codec.CodecUtils.writeUtf8String;

public class ClpTableLayoutHandleCodec
        implements ConnectorCodec<ConnectorTableLayoutHandle>
{
    // Wire format (C++ ClpConnectorProtocol::deserialize for ConnectorTableLayoutHandle):
    //   [table handle]     : serialized by ClpTableHandleCodec
    //   kqlQueryPresent    : writeBoolean
    //   [kqlQuery]         : 2-byte BE length + UTF-8 bytes, if present
    //   metadataSqlPresent : writeBoolean
    //   [metadataSql]      : 2-byte BE length + UTF-8 bytes, if present

    @Override
    public byte[] serialize(ConnectorTableLayoutHandle handle)
    {
        try {
            if (!(handle instanceof ClpTableLayoutHandle)) {
                throw new IllegalArgumentException("Expected ClpTableLayoutHandle but got: " +
                        (handle == null ? "null" : handle.getClass().getName()));
            }
            ClpTableLayoutHandle layoutHandle = (ClpTableLayoutHandle) handle;
            ByteArrayOutputStream byteOut = new ByteArrayOutputStream();
            DataOutputStream out = new DataOutputStream(byteOut);
            writeTableHandle(layoutHandle.getTable(), out);
            // Serialize kqlQuery
            Optional<String> kqlQuery = layoutHandle.getKqlQuery();
            out.writeBoolean(kqlQuery.isPresent());
            if (kqlQuery.isPresent()) {
                writeUtf8String(kqlQuery.get(), out);
            }
            // Serialize metadataSql
            Optional<String> metadataSql = layoutHandle.getMetadataSql();
            out.writeBoolean(metadataSql.isPresent());
            if (metadataSql.isPresent()) {
                writeUtf8String(metadataSql.get(), out);
            }
            return byteOut.toByteArray();
        }
        catch (IOException e) {
            throw new UncheckedIOException("Failed to serialize ClpTableLayoutHandle", e);
        }
    }

    @Override
    public ConnectorTableLayoutHandle deserialize(byte[] bytes)
    {
        try {
            DataInputStream in = new DataInputStream(new ByteArrayInputStream(bytes));
            ClpTableHandle table = readTableHandle(in);
            // Deserialize kqlQuery
            Optional<String> kqlQuery = in.readBoolean()
                    ? Optional.of(readUtf8String(in))
                    : Optional.empty();
            // Deserialize metadataSql
            Optional<String> metadataSql = in.readBoolean()
                    ? Optional.of(readUtf8String(in))
                    : Optional.empty();
            if (in.available() > 0) {
                throw new IOException("Unexpected trailing bytes in ClpTableLayoutHandle deserialization");
            }
            return new ClpTableLayoutHandle(table, kqlQuery, metadataSql);
        }
        catch (IOException e) {
            throw new UncheckedIOException("Failed to deserialize ClpTableLayoutHandle", e);
        }
    }
}
