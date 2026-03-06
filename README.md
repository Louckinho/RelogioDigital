# 🕒 Relógio IoT Autônomo de Alta Estabilidade

Este projeto consiste em um relógio digital autônomo desenhado com foco em estabilidade elétrica, eficiência térmica e segurança de memória. Ele utiliza um microcontrolador ESP8266 (NodeMCU) orquestrando um display de matriz de LEDs (MAX7219) e um Módulo RTC (DS3231), com sincronização inteligente via NTP (Network Time Protocol).

O firmware foi construído com um mindset de engenharia de sistemas industriais: isolando falhas de leitura de hardware, prevenindo *Brownout Resets* (quedas de tensão) no boot e otimizando o ciclo de CPU.

---

## ⚙️ Arquitetura do Sistema e Topologia Elétrica

O gargalo crítico em projetos com matriz de LED é o pico de corrente inicial. O ESP8266 exige até 400mA ao ligar o rádio Wi-Fi, enquanto o display MAX7219 pode puxar até 1000mA (1A). Se ligados em série no regulador 3.3V do NodeMCU, o sistema sofre colapso de tensão.

A arquitetura adota a **Alimentação Paralela**, utilizando o pino `VIN` (5V direto da USB/Fonte) para alimentar o display pesado, poupando a ECU primária.

**Diagrama de Fluxo de Energia e Dados:**

    [ Fonte 5V / 3A ] 
            |
            v
    [ Pino VIN (5V) ] -----------------------> [ VCC MAX7219 ] (Força Bruta)
            |
            v
    [ Regulador 3.3V ]
            |
            v
    [ NodeMCU ESP8266 ] (Cérebro)
      /             \
    (I2C Bus)      (SPI Bus)
     /                 \
    [ DS3231 RTC ]    [ MAX7219 Display ]


### 🔌 Pinagem (Wiring)

| Componente | Pino Físico | Conexão no NodeMCU | Notas de Engenharia |
| :--- | :--- | :--- | :--- |
| **MAX7219** | VCC | `VIN` ou `VU` | **CRÍTICO:** Nunca use o `3V3`. Use os 5V brutos. |
| **MAX7219** | GND | `GND` | Aterramento comum. |
| **MAX7219** | DIN | `D7` (MOSI) | Hardware SPI. |
| **MAX7219** | CS | `D6` | Configurado no código. |
| **MAX7219** | CLK | `D5` (SCK) | Hardware SPI. |
| **DS3231** | VCC | `3V3` | Opera perfeitamente em 3.3V lógicos. |
| **DS3231** | GND | `GND` | Aterramento comum. |
| **DS3231** | SDA | `D2` | Barramento I2C (Dados). |
| **DS3231** | SCL | `D1` | Barramento I2C (Clock). |

---

## 🧠 Trade-offs de Engenharia e Lógica Aplicada

Para garantir que o relógio opere continuamente sem intervenção manual, o projeto foi otimizado (min-maxing) nos seguintes pontos:

* **Soft-Starter Elétrico:** O firmware corta o Wi-Fi imediatamente no boot e inicializa o display com brilho 0. Isso evita a soma de picos de consumo e previne falhas de inicialização (*Brownouts*).
* **Blindagem de Memória C++:** Módulos RTC podem retornar ruído (ex: erro 255) em caso de vibração no cabo I2C. Para evitar *Kernel Panics* (Exception 4 / Memory Leak) ao ler Arrays fora do limite, foi implementada uma estrutura estática `Switch/Case`. O erro de hardware é absorvido e o software nunca trava.
* **Máquina de Estados Assíncrona:** A esteira principal (`void loop`) não possui paradas (`delay`). O display alterna fluidamente entre Hora (20s) e Data (3s) baseando-se no cronômetro interno `millis()`, mantendo os comandos seriais responsivos.
* **Desligamento Térmico do Rádio:** O Wi-Fi só é "acordado" 1 vez a cada 24 horas para parear com relógios atômicos (`pool.ntp.org`). Ao finalizar, o rádio é isolado para evitar aquecimento cruzado próximo ao sensor do RTC.

---

## 🚀 Instalação e Compilação

Na sua IDE do Arduino (v1.8+), instale as seguintes dependências via **Gerenciador de Bibliotecas**:

1.  `RTClib` (por Adafruit)
2.  `MD_Parola` (por majicDesigns)
3.  `MD_MAX72XX` (por majicDesigns)
4.  `NTPClient` (por Fabrice Weinberg)

> **Importante:** Edite as variáveis `ssid` e `password` no início do código com os dados da sua rede Wi-Fi local antes de compilar.

---

## 🛠️ Interface e HUD de Telemetria

O dispositivo oferece um HUD via **Monitor Serial (115200 baud)** para testes de bancada. Ele imprime a telemetria a cada 10 segundos e aceita as seguintes entradas em tempo real:

* Digite `ajustar`: Abre o assistente para cravar a hora física no formato (AAAA-MM-DD HH:MM:SS).
* Digite `ntp -3`: Força sincronização instantânea com o servidor NTP (Ajuste o `-3` para o seu fuso horário).
* Digite de `0` a `15`: Altera o brilho da matriz de LED instantaneamente.

---

## 🚨 Pre-Mortem (Debugging Preventivo)

Caso o sistema apresente instabilidades na bancada, verifique os seguintes gargalos físicos:

1. **Placa reinicia em loop ou display apaga do nada:**
   * **Causa Diagnóstica:** Queda de tensão (*Brownout Reset*).
   * **Ação Preventiva:** Verifique a bitola (espessura) do seu cabo USB. Fios finos possuem alta resistência e derrubam os 5V da fonte quando exigidos. Certifique-se de que o MAX7219 está ligado no pino `VIN` e nunca no `3V3`.
2. **Display mostra datas bizarras (ex: "??? 165"):**
   * **Causa Diagnóstica:** Perda de comunicação no barramento I2C.
   * **Ação Preventiva:** O software absorveu a falha, mas o hardware desconectou. Verifique o aperto dos jumpers em D1 e D2 e messa a carga da bateria CR2032 do módulo DS3231.
