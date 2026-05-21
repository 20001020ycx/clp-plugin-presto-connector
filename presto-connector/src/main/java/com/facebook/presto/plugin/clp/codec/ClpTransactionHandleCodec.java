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
package com.facebook.presto.plugin.clp.codec;

import com.facebook.presto.spi.ConnectorCodec;
import com.facebook.presto.spi.connector.ConnectorTransactionHandle;

import com.facebook.presto.plugin.clp.ClpTransactionHandle;

public class ClpTransactionHandleCodec implements ConnectorCodec<ConnectorTransactionHandle> {
    // Wire format (C++ ClpConnectorProtocol::deserialize for ConnectorTransactionHandle):
    // (0 bytes — empty payload)

    @Override
    public byte[] serialize(ConnectorTransactionHandle handle) {
        if (handle != ClpTransactionHandle.INSTANCE) {
            throw new IllegalArgumentException(
                    "Expected ClpTransactionHandle but got: " + handle.getClass().getName()
            );
        }
        return new byte[0];
    }

    @Override
    public ConnectorTransactionHandle deserialize(byte[] bytes) {
        if (bytes.length > 0) {
            throw new IllegalArgumentException(
                    "Expected empty payload for ClpTransactionHandle but got " + bytes.length
                            + " bytes"
            );
        }
        return ClpTransactionHandle.INSTANCE;
    }
}
