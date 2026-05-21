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

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/**
 * String serialization helpers for the binary codec wire format.
 * Wire format: 2-byte big-endian length + standard UTF-8 bytes.
 *
 * <p>We use these instead of {@code DataOutputStream.writeUTF} / {@code DataInputStream.readUTF}
 * because those methods use Java's
 * <a href="https://docs.oracle.com/javase/8/docs/api/java/io/DataInput.html#modified-utf-8">Modified UTF-8</a>,
 * which differs from standard UTF-8 for null bytes ({@code 0xC0 0x80} vs {@code 0x00}) and
 * supplementary characters (surrogate pairs vs 4-byte sequences). The C++ side deserialize the encoding to std::strings
 * as raw bytes and passes them to clp-s, which assumes standard UTF-8 — so Modified UTF-8
 * would break string comparisons for some unicode content.
 */
final class CodecUtils
{
    private CodecUtils() {}

    static void writeUtf8String(String value, DataOutputStream out)
            throws IOException
    {
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        if (bytes.length > 0xFFFF) {
            throw new IOException("UTF-8 string too long for 2-byte length prefix: " + bytes.length + " bytes");
        }
        out.writeShort(bytes.length);
        out.write(bytes);
    }

    static String readUtf8String(DataInputStream in)
            throws IOException
    {
        int length = in.readUnsignedShort();
        byte[] bytes = new byte[length];
        in.readFully(bytes);
        return new String(bytes, StandardCharsets.UTF_8);
    }
}
