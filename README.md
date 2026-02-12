![LocalDrop Banner](./assets/LocalDrop.png)

# 📡 LocalDrop

> Ferramenta de transferência de arquivos leve, open-source e voltada para rede local, inspirada no AirDrop.

O LocalDrop descobre dispositivos próximos na mesma rede e permite que você envie arquivos de forma segura através de uma interface simples ou via CLI rápida. Construído com um núcleo em C para máxima velocidade e portabilidade, foi projetado para funcionar offline e com foco total em privacidade.

## ✨ Funcionalidades

- **Configuração Zero**: Descoberta automática de dispositivos usando mDNS (Avahi).
- **Interface Web**: Interface simples acessível pelo navegador para dispositivos móveis e desktop.
- **Cross-Platform**: Motor principal escrito em C para alto desempenho.
- **Pareamento via QR Code**: Conexão instantânea para celulares escaneando o QR code gerado no terminal.
- **Privacidade em Primeiro Lugar**: Os arquivos permanecem na sua rede local. Sem nuvem, sem rastreamento.
- **Rápido**: Transferências em alta velocidade limitadas apenas pelo seu hardware de rede.

## 🛠 Pré-requisitos

Para buildar e rodar o LocalDrop, você precisará das seguintes bibliotecas instaladas:

- **libmicrohttpd**: Para o servidor web interno.
- **libqrencode**: Para gerar QR codes no terminal.
- **avahi-client & avahi-common**: Para a descoberta mDNS.
- **OpenSSL**: Para futuras transferências criptografadas.

No Ubuntu/Debian:
```bash
sudo apt update
sudo apt install libmicrohttpd-dev libqrencode-dev libavahi-client-dev libavahi-common-dev libssl-dev cmake build-essential
```

## 🚀 Como Começar

### Compilando do Código Fonte

1. Clone o repositório:
   ```bash
   git clone https://github.com/schnnjuan/LocalDrop.git
   cd LocalDrop
   ```

2. Crie um diretório de build e compile:
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ```

### Executando

Inicie o servidor:
```bash
./localdrop
```

Ao iniciar, o LocalDrop irá:
1. Se anunciar na rede local.
2. Gerar um QR code no seu terminal.
3. Iniciar um servidor web na porta `8080`.

Acesse a interface escaneando o QR code com seu celular ou abrindo a URL exibida no seu navegador.

## 🏗 Arquitetura

O LocalDrop é estruturado para simplicidade e performance:

- **`src/main.c`**: Ponto de entrada e gerenciamento do servidor HTTP.
- **`src/discovery.c`**: Implementação mDNS usando Avahi.
- **`src/server.c`**: Processamento de requisições e roteamento de arquivos.
- **`src/transfer.c`**: Lógica central para envio e recebimento de arquivos.
- **`src/ui.c`**: Interface de terminal e relatórios de status.

## 📝 Licença

Distribuído sob a licença MIT. Veja `LICENSE` para mais informações.

## 🤝 Agradecimentos

- Inspirado pelo AirDrop da Apple.
- Construído com [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/) e [Avahi](https://avahi.org/).

---
*Personagem inspirada na gata da minha namorada* 🐱
