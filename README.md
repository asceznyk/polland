# Rgnx

**Rgnx** is a minimal, experimental HTTP server inspired by the architecture and philosophy of **NGINX**.
The goal of the project is to understand how high-performance web servers work internally by building one from scratch.

This project is currently **a work in progress**. The design, features, and APIs are actively evolving.

---


## Overview

Rgnx is a small web server that explores core ideas behind modern event-driven servers such as:

* Non-blocking network I/O
* Event-driven request handling
* Efficient connection management
* Lightweight request parsing
* Minimal overhead HTTP responses

Rather than aiming to be production-ready, the focus is on **learning, experimentation, and systems design**.

---


## Implemented Features

Current functionality includes:

* Basic TCP server
* HTTP request parsing - `GET`, `HEAD`
* HTTP response generation - `GET`, `HEAD`
* Event-loop based request handling
* Static response handling

More features such as routing, configuration parsing, and better connection management are planned.

---


## Installation (Linux)

### 1. Clone the repository

```bash
git clone https://github.com/asceznyk/rgnx.git
cd rgnx
```

### 2. Build the project

```bash
make
```

### 3. Run the server

```bash
./out/server
```

By default the server starts on:

```
http://localhost:6969
```

---


## Status

Rgnx is **early stage** and not intended for any kind of actual use.

The primary goal is learning how a system like **NGINX** works internally.

---



