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
package com.facebook.presto.plugin.clp.split.filter;

import static com.facebook.presto.plugin.clp.split.filter.ClpSplitFilterConfig.CustomSplitFilterOptions;

import com.google.inject.Inject;

import com.facebook.presto.plugin.clp.ClpConfig;

/**
 * Split filter provider for the integration-test providers. Splits come from a directory listing
 * rather than a queryable metadata store, so there is nothing to push a metadata filter down to.
 */
public class ClpIntegrationTestSplitFilterProvider extends ClpSplitFilterProvider {
    @Inject
    public ClpIntegrationTestSplitFilterProvider(ClpConfig config) {
        super(config);
    }

    @Override
    public String remapSplitFilterPushDownExpression(String scope, String pushDownExpression) {
        return pushDownExpression;
    }

    @Override
    protected Class<? extends CustomSplitFilterOptions> getCustomSplitFilterOptionsClass() {
        return CustomSplitFilterOptions.class;
    }
}
