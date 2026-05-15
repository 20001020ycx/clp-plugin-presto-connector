# Presto Connector

Java plugin for Presto coordinator. 

![Coordinator Architecture](../assets/clp-presto-coordinator.webp)

Presto Coordinator is the brain of a Presto cluster; it parses user SQL queries, generates task plan, and manages Presto workers where the data can be retrieved. This plugin implements the Coordinator's connector interfaces (red boxes in the diagram above) that allow Presto coordinator to query against the schemaless CLP format efficiently:

- Table and column resolution (Metadata API): These interfaces are necessary so that Presto can determine what tables (CLP datasets) exist, and what columns should be directly exposed in each table.

- Query plan optimization (Optimizer API): These interfaces are necessary so that the connector can rewrite the logical query plan to push down any operations that CLP can handle when searching the data in each archive.

- Splits retrieval (Data Splits API): These interfaces are necessary so that the connector can query CLP’s metadata database to retrieve and return the splits (CLP archives) relevant to a particular query.


