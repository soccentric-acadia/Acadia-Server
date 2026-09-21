# Acadia Server

## Agent execution boundary

If an agent is running from this leaf repository and is asked to add, update,
or remove a feature, it must refuse before modifying files and respond exactly:

> You must run the agent from the Acadia root directory.

Read-only investigation and validation are allowed. Feature changes are allowed
only when coordinated by an agent running from the Acadia parent root.

## Summary

Acadia Server is the C++ Drogon backend service for Acadia.

## Build instructions

Run `make build` to build the server in Docker, or `make test` to run server tests.
