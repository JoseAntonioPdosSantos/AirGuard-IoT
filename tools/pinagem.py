#!/usr/bin/env python3
"""Folha de pinagem do AirGuard para impressao em A4 (sensor ESP32 + receptor RP2040-Zero).

    python3 tools/pinagem.py            -> docs/pinagem/pinagem.html
    google-chrome --headless --no-pdf-header-footer \\
        --print-to-pdf=docs/pinagem/AirGuard-pinagem.pdf docs/pinagem/pinagem.html

Os numeros dos pinos sao lidos dos proprios firmwares (src/config/hardware_pins.h e
receiver/src/rx_config.h): a folha nao fica diferente do codigo. O que nao esta no codigo
(resistores, alimentacao, contatos do rele) vem de docs/01_hardware.md e receiver/README.md.
Os desenhos sao esquematicos: mostram QUAL pino liga em QUE, nao a posicao fisica do pino na
placa (ela muda entre as placas ESP32 de 30 e de 38 pinos). Siga o nome impresso na placa.
"""
import datetime
import html
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "pinagem" / "pinagem.html"


def consts(path):
    text = (ROOT / path).read_text(encoding="utf-8")
    return {m[1]: int(m[2]) for m in re.finditer(r"constexpr\s+uint8_t\s+(\w+)\s*=\s*(\d+)\s*;", text)}


N = consts("src/config/hardware_pins.h")   # sensor (ESP32)
R = consts("receiver/src/rx_config.h")     # receptor (RP2040-Zero)


def gpio(name):
    return f"GPIO{N[name]}"


def gp(name):
    return f"GP{R[name]}"


# ---------------------------------------------------------------------------------------------
# Desenho (SVG)
# ---------------------------------------------------------------------------------------------
W = 900
ROW = 30      # altura de uma linha de pino
TH = 42       # altura do titulo de um modulo
LED_COLOR = {"vermelho": "#d32f2f", "amarelo": "#f9a825", "azul": "#1565c0", "verde": "#2e7d32"}
PILL = {"3V3": "#c62828", "5V": "#e65100", "GND": "#1c1c1c"}


def esc(s):
    return html.escape(str(s), quote=True)


class Svg:
    def __init__(self, height):
        self.h = height
        self.o = []

    def line(self, x1, y1, x2, y2, cls="wire"):
        self.o.append(f'<line class="{cls}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}"/>')

    def rect(self, x, y, w, h, cls, rx=0, style=""):
        st = f' style="{style}"' if style else ""
        self.o.append(f'<rect class="{cls}" x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}"{st}/>')

    def text(self, x, y, t, cls="pin", anchor="start", style=""):
        st = f' style="{style}"' if style else ""
        self.o.append(f'<text class="{cls}" x="{x}" y="{y}" dy=".35em" text-anchor="{anchor}"{st}>{esc(t)}</text>')

    def dot(self, x, y):
        self.o.append(f'<circle class="dot" cx="{x}" cy="{y}" r="4"/>')

    def pill(self, x, y, label, anchor="start"):
        """Ponto de alimentacao (3V3 / 5V / GND): nao tem fio desenhado ate a placa."""
        kind = "5V" if label.startswith("5V") else label
        w = round(8.6 * len(label) + 18)
        x0 = x if anchor == "start" else x - w if anchor == "end" else x - w / 2
        self.rect(x0, y - 11, w, 22, "pill", rx=11, style=f"fill:{PILL[kind]}")
        self.text(x0 + w / 2, y, label, "pilltext", "middle")
        return x0, x0 + w

    def resistor(self, x, y, label):
        """Resistor num fio horizontal, comecando em x. Devolve o x do fim."""
        w = round(7.6 * len(label) + 16)
        self.rect(x, y - 11, w, 22, "res", rx=3)
        self.text(x + w / 2, y, label, "restext", "middle")
        return x + w

    def resistor_v(self, x, y, label, side="right"):
        """Resistor num fio vertical, de y a y+46, com o valor ao lado."""
        self.rect(x - 10, y, 20, 46, "res", rx=3)
        if side == "right":
            self.text(x + 15, y + 23, label, "restext")
        else:
            self.text(x - 15, y + 23, label, "restext", "end")
        return y + 46

    def led(self, x, y, color):
        """LED apontando para a direita (anodo em x). Devolve o x do catodo."""
        self.o.append(f'<polygon class="led" points="{x},{y - 11} {x},{y + 11} {x + 20},{y}" style="fill:{LED_COLOR[color]}"/>')
        self.line(x + 20, y - 11, x + 20, y + 11, "ledbar")
        return x + 20

    def module(self, x, y, w, title, sub, rows, side):
        """Modulo com os pinos na borda `side`. Devolve {pino: (x, y)} do ponto de ligacao."""
        h = TH + ROW * len(rows)
        self.rect(x, y, w, h, "box", rx=6)
        self.o.append(f'<path class="boxhead" d="M{x + 6},{y} h{w - 12} a6,6 0 0 1 6,6 v{TH - 6} h{-w} v{-(TH - 6)} a6,6 0 0 1 6,-6 z"/>')
        self.text(x + 10, y + 14, title, "title")
        self.text(x + 10, y + 31, sub, "small")
        edge = x + w if side == "right" else x
        pts = {}
        for i, name in enumerate(rows):
            py = y + TH + ROW * i + ROW / 2
            if name.startswith("#"):
                self.line(x, py - ROW / 2 + 4, x + w, py - ROW / 2 + 4, "sep")
                self.text(x + w / 2, py + 3, name[1:], "small", "middle")
                continue
            self.rect(edge - 5, py - 5, 10, 10, "pad")
            if side == "right":
                self.text(edge - 12, py, name, "pin", "end")
            else:
                self.text(edge + 12, py, name, "pin")
            pts[name] = (edge, py)
        return pts, y + h

    def mcu(self, x, y, w, h, title, sub, left, right, power, power_y):
        self.rect(x, y, w, h, "mcu", rx=8)
        self.text(x + w / 2, y + 22, title, "mcutitle", "middle")
        self.text(x + w / 2, y + 41, sub, "small", "middle")
        for py, label in left:
            self.rect(x - 5, py - 5, 10, 10, "pad")
            self.text(x + 12, py, label, "mcupin")
        for py, label in right:
            self.rect(x + w - 5, py - 5, 10, 10, "pad")
            self.text(x + w - 12, py, label, "mcupin", "end")
        py = power_y
        self.text(x + w / 2, py, "alimentação", "small", "middle")
        for i, label in enumerate(power):
            self.pill(x + w / 2, py + 26 + 28 * i, label, "middle")

    def render(self):
        return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 30 {W} {self.h - 30}" role="img">'
                + "".join(self.o) + "</svg>")


LX, LW = 8, 180            # modulos da esquerda
MX, MW = 350, 190          # placa
LEDGE, REDGE = LX + LW, MX + MW
RX, RW = 740, 152          # modulo da direita (rele)


def power_row(d, pt, label):
    x, y = pt
    d.line(x, y, x + 18, y, "pwire")
    d.pill(x + 18, y, label)


def chain(d, y, caption, value, color=None, part=None):
    """Da placa (direita): resistor -> LED (ou outra peca) -> GND."""
    d.text(REDGE + 32, y - 21, caption, "small")
    d.line(REDGE, y, REDGE + 32, y)
    x = d.resistor(REDGE + 32, y, value)
    d.line(x, y, x + 22, y)
    x += 22
    if color:
        x = d.led(x, y, color)
    else:
        w = round(7.6 * len(part) + 16)
        d.rect(x, y - 13, w, 26, "part", rx=4)
        d.text(x + w / 2, y, part, "restext", "middle")
        x += w
    d.line(x, y, x + 18, y, "pwire")
    d.pill(x + 18, y, "GND")


def draw_esp32():
    d = Svg(905)
    # --- esquerda: modulos com fios de dados retos ate a placa ---
    ry, y = d.module(LX, 40, LW, "RYLR998", "rádio LoRa · só 3,3 V", ["VDD", "GND", "TXD", "RXD", "NRST"], "right")
    ol, y = d.module(LX, y + 22, LW, "OLED SSD1306", "0,96\" I2C · só 3,3 V", ["VCC", "GND", "SDA", "SCL"], "right")
    dh, y = d.module(LX, y + 22, LW, "DHT11", "temperatura / umidade", ["VCC", "GND", "S (DATA)"], "right")
    mq, y = d.module(LX, y + 22, LW, "MQ-135", "gás · aquecedor em 5 V", ["VCC", "GND", "DO", "AO"], "right")
    left = [(ry["TXD"][1], f"{gpio('LORA_UART_RX')} · RX2"), (ry["RXD"][1], f"{gpio('LORA_UART_TX')} · TX2"),
            (ry["NRST"][1], gpio("LORA_NRST")), (ol["SDA"][1], f"{gpio('I2C_SDA')} · SDA"),
            (ol["SCL"][1], f"{gpio('I2C_SCL')} · SCL"), (dh["S (DATA)"][1], gpio("DHT_DATA")),
            (mq["AO"][1], f"{gpio('MQ135_ADC')} · ADC1")]
    for pt in (ry["TXD"], ry["RXD"], ry["NRST"], ol["SDA"], ol["SCL"], dh["S (DATA)"]):
        d.line(pt[0], pt[1], MX, pt[1])
    power_row(d, ry["VDD"], "3V3"); power_row(d, ry["GND"], "GND")
    power_row(d, ol["VCC"], "3V3"); power_row(d, ol["GND"], "GND")
    power_row(d, dh["VCC"], "3V3"); power_row(d, dh["GND"], "GND")
    power_row(d, mq["VCC"], "5V (VIN)"); power_row(d, mq["GND"], "GND")
    d.text(mq["DO"][0] + 16, mq["DO"][1], "não ligar", "small")
    # MQ-135: divisor obrigatorio 10k / (10k + 10k)
    ax, ay = mq["AO"]
    jx = 296
    d.line(ax, ay, ax + 20, ay)
    x = d.resistor(ax + 20, ay, "10 kΩ")
    d.line(x, ay, MX, ay)
    d.dot(jx, ay)
    d.line(jx, ay, jx, ay + 18)
    yy = d.resistor_v(jx, ay + 18, "10 kΩ")
    d.line(jx, yy, jx, yy + 8)
    yy = d.resistor_v(jx, yy + 8, "10 kΩ")
    d.line(jx, yy, jx, yy + 12, "pwire")
    d.pill(jx, yy + 23, "GND", "middle")
    d.text(jx - 18, ay + 64, "divisor", "small", "end")
    d.text(jx - 18, ay + 79, "obrigatório", "small", "end")

    # --- direita: buzzer, LEDs, rele ---
    right = []
    for y, cap, val, color, pin in ((110, "Buzzer ativo", "1 kΩ", None, "BUZZER"),
                                    (175, "LED vermelho", "1 kΩ", "vermelho", "LED_RED"),
                                    (240, "LED amarelo", "1 kΩ", "amarelo", "LED_YELLOW"),
                                    (305, "LED azul · mensagem do WhatsApp", "230 Ω", "azul", "LED_MSG")):
        chain(d, y, cap, val, color, "Buzzer + −")
        right.append((y, gpio(pin)))
    rl, _ = d.module(RX, 360, RW, "Relé", "módulo 3 V · nível alto",
                     ["IN", "VCC", "GND", "#contatos", "NC", "COM", "NO"], "left")
    d.line(REDGE, rl["IN"][1], RX, rl["IN"][1])
    right.append((rl["IN"][1], gpio("RELAY")))
    for name, label in (("VCC", "3V3"), ("GND", "GND"), ("COM", "GND")):
        x, y = rl[name]
        d.line(x - 18, y, x, y, "pwire")
        d.pill(x - 18, y, label, "end")
    # LED verde: 3V3 -> 1k -> LED -> contato NC (sem GPIO)
    x, y = rl["NC"]
    _, px = d.pill(REDGE + 26, y, "3V3")
    d.line(px, y, px + 12, y, "pwire")
    rx_end = d.resistor(px + 12, y, "1 kΩ")
    d.line(rx_end, y, rx_end + 18, y)
    lx = d.led(rx_end + 18, y, "verde")
    d.line(lx, y, x, y)
    d.text(px - 44, y - 21, "LED verde (sem GPIO)", "small")
    d.text(rl["NO"][0] - 14, rl["NO"][1], "livre", "small", "end")

    d.mcu(MX, 40, MW, 722, "ESP32 DevKit", "ESP32-WROOM-32", left, right, ["3V3", "GND", "5V (VIN)"], 600)
    return d.render()


def draw_rp2040():
    d = Svg(520)
    ry, y = d.module(LX, 40, LW, "RYLR998", "rádio LoRa · só 3,3 V", ["VDD", "GND", "TXD", "RXD", "NRST"], "right")
    lc, y = d.module(LX, y + 22, LW, "LCD 16x2 + I2C", "PCF8574 · alimentado em 5 V", ["GND", "VCC", "SDA", "SCL"], "right")
    left = [(ry["TXD"][1], f"{gp('LORA_UART_RX')} · RX"), (ry["RXD"][1], f"{gp('LORA_UART_TX')} · TX"),
            (ry["NRST"][1], gp("LORA_NRST")), (lc["SDA"][1], f"{gp('I2C_SDA')} · SDA"),
            (lc["SCL"][1], f"{gp('I2C_SCL')} · SCL")]
    for pt in (ry["TXD"], ry["RXD"], ry["NRST"], lc["SDA"], lc["SCL"]):
        d.line(pt[0], pt[1], MX, pt[1])
    power_row(d, ry["VDD"], "3V3"); power_row(d, ry["GND"], "GND")
    power_row(d, lc["VCC"], "5V"); power_row(d, lc["GND"], "GND")
    # 10k de cada linha I2C ao GND (o LCD em 5 V puxa as linhas para 5 V)
    jx = 312
    sx, sy = lc["SDA"]
    d.dot(jx, sy)
    d.line(jx, sy, jx, sy - 14)
    d.resistor_v(jx, sy - 60, "10 kΩ", "left")
    d.line(jx, sy - 60, jx, sy - 72, "pwire")
    d.pill(jx, sy - 83, "GND", "middle")
    cx, cy = lc["SCL"]
    d.dot(jx, cy)
    d.line(jx, cy, jx, cy + 14)
    yy = d.resistor_v(jx, cy + 14, "10 kΩ", "left")
    d.line(jx, yy, jx, yy + 12, "pwire")
    d.pill(jx, yy + 23, "GND", "middle")

    right = []
    for y, cap, color, pin in ((120, "LED verde", "verde", "LED_GREEN"), (185, "LED amarelo", "amarelo", "LED_YELLOW"),
                               (250, "LED vermelho", "vermelho", "LED_RED")):
        chain(d, y, cap, "220 Ω a 1 kΩ", color)
        right.append((y, gp(pin)))
    d.mcu(MX, 40, MW, 450, "RP2040-Zero", "Waveshare", left, right, ["3V3", "GND", "5V"], 264)
    return d.render()


# ---------------------------------------------------------------------------------------------
# Tabelas
# ---------------------------------------------------------------------------------------------
def table(rows):
    """rows: (componente | None para repetir, pino do modulo, liga em, observacao)."""
    out = ['<table class="pins"><thead><tr><th class="chk">ok</th><th>Componente</th><th>Pino do módulo</th>'
           "<th>Liga em</th><th>Observação</th></tr></thead><tbody>"]
    for comp, pin, to, note in rows:
        cls = ' class="first"' if comp else ""
        out.append(f'<tr{cls}><td class="chk"><span class="box"></span></td><td class="comp">{comp or ""}</td>'
                   f"<td class=\"mp\">{pin}</td><td class=\"to\">{to}</td><td class=\"note\">{note}</td></tr>")
    out.append("</tbody></table>")
    return "".join(out)


def b(s):
    return f"<b>{s}</b>"


ESP32_ROWS = [
    ("RYLR998 (LoRa)", "VDD", b("3V3"), "só 3,3 V"),
    (None, "GND", "GND", ""),
    (None, "TXD", b(f"{gpio('LORA_UART_RX')}") + " (RX2)", "o TXD do rádio entra no RX do ESP32"),
    (None, "RXD", b(f"{gpio('LORA_UART_TX')}") + " (TX2)", ""),
    (None, "NRST", b(gpio("LORA_NRST")), "reset do rádio, ativo em nível baixo"),
    ("OLED SSD1306", "VCC", b("3V3"), "nunca 5 V: os pull-ups do módulo levariam 5 V aos GPIO"),
    (None, "GND", "GND", ""),
    (None, "SDA", b(gpio("I2C_SDA")), ""),
    (None, "SCL", b(gpio("I2C_SCL")), ""),
    ("DHT11 (módulo)", "VCC", b("3V3"), ""),
    (None, "GND", "GND", ""),
    (None, "S (DATA)", b(gpio("DHT_DATA")), "manter a 5 cm ou mais do MQ-135, que esquenta"),
    ("MQ-135 (módulo)", "VCC", b("VIN (5 V)"), "aquecedor de ~160 mA"),
    (None, "GND", "GND", ""),
    (None, "AO", f"10 kΩ → {b(gpio('MQ135_ADC'))}", f"e 2× 10 kΩ em série de {gpio('MQ135_ADC')} ao GND. Nunca o AO direto"),
    (None, "DO", "não ligar", "saída de 5 V"),
    ("Buzzer ativo", "+", f"1 kΩ → {b(gpio('BUZZER'))}", "(−) no GND"),
    ("LED vermelho", "ânodo (perna longa)", f"1 kΩ → {b(gpio('LED_RED'))}", "cátodo no GND"),
    ("LED amarelo", "ânodo", f"1 kΩ → {b(gpio('LED_YELLOW'))}", "cátodo no GND"),
    ("LED azul (mensagem)", "ânodo", f"230 Ω → {b(gpio('LED_MSG'))}", "cátodo no GND"),
    ("Relé (módulo 3 V)", "IN", b(gpio("RELAY")), "nível alto aciona"),
    (None, "VCC", b("3V3"), "bobina de 3 V. Módulo de 5 V: ver docs/01_hardware.md, 4.4"),
    (None, "GND", "GND", ""),
    (None, "COM", "GND", "contato do relé"),
    (None, "NC", "cátodo do LED verde", "contato do relé; o NO fica livre"),
    ("LED verde", "ânodo", "1 kΩ → " + b("3V3"), "sem GPIO: apaga quando o relé aciona"),
]

RP2040_ROWS = [
    ("RYLR998 (LoRa)", "VDD", b("3V3"), "só 3,3 V"),
    (None, "GND", "GND", ""),
    (None, "TXD", b(gp("LORA_UART_RX")), "o TXD do rádio entra no RX da placa"),
    (None, "RXD", b(gp("LORA_UART_TX")), ""),
    (None, "NRST", b(gp("LORA_NRST")), "reset do rádio, ativo em nível baixo"),
    ("LCD 16x2 + I2C", "VCC", b("5V"), "jumper direto no pino 5V, não no trilho do 3V3 do rádio"),
    (None, "GND", "GND", ""),
    (None, "SDA", b(gp("I2C_SDA")), "mais 10 kΩ desta linha ao GND"),
    (None, "SCL", b(gp("I2C_SCL")), "mais 10 kΩ desta linha ao GND"),
    ("LED verde", "ânodo (perna longa)", f"220 Ω a 1 kΩ → {b(gp('LED_GREEN'))}", "cátodo no GND"),
    ("LED amarelo", "ânodo", f"220 Ω a 1 kΩ → {b(gp('LED_YELLOW'))}", "cátodo no GND"),
    ("LED vermelho", "ânodo", f"220 Ω a 1 kΩ → {b(gp('LED_RED'))}", "cátodo no GND"),
]

ESP32_LEDS = [
    ("verde", "Verde", "aceso: relé desligado (normal). Apagado: relé acionado (alerta HIGH)"),
    ("amarelo", "Amarelo", "aceso: alerta LOW. Piscando rápido (2 Hz): MEDIUM. Piscando devagar (1 Hz): partida / aquecimento do MQ-135"),
    ("vermelho", "Vermelho", "piscando (2 Hz): alerta HIGH"),
    ("azul", "Azul", "pisca rápido por 5 s quando chega uma mensagem do WhatsApp; um alerta interrompe"),
]
RP2040_LEDS = [
    ("verde", "Verde", "aceso: tudo normal. Piscando devagar: nada recebido ainda. Piscando rápido: mensagem do WhatsApp no LCD"),
    ("amarelo", "Amarelo", "aceso: alerta LOW ou MEDIUM. Piscando: sem sinal do sensor há mais de 15 s"),
    ("vermelho", "Vermelho", "aceso: alerta HIGH"),
]


def leds(items):
    return '<table class="leds"><tbody>' + "".join(
        f'<tr><td><span class="lamp" style="background:{LED_COLOR[c]}"></span>{name}</td><td>{what}</td></tr>'
        for c, name, what in items) + "</tbody></table>"


def bullets(items):
    return "<ul>" + "".join(f"<li>{i}</li>" for i in items) + "</ul>"


CSS = """
@page { size: A4 portrait; margin: 11mm 12mm 11mm 12mm; }
* { box-sizing: border-box; }
html { -webkit-print-color-adjust: exact; print-color-adjust: exact; }
body { margin: 0; font-family: "Liberation Sans", Arial, Helvetica, sans-serif; color: #111; font-size: 9.5pt; line-height: 1.32; }
.sheet { width: 186mm; margin: 0 auto; break-after: page; page-break-after: always; }
.sheet:last-child { break-after: auto; page-break-after: auto; }
header { display: flex; justify-content: space-between; align-items: flex-end; border-bottom: 1.2pt solid #111; padding-bottom: 2mm; margin-bottom: 3mm; }
header h1 { font-size: 15pt; margin: 0; line-height: 1.15; }
header .board { font-size: 10.5pt; color: #333; margin-top: .6mm; }
header .meta { text-align: right; font-size: 8pt; color: #444; line-height: 1.35; }
h2 { font-size: 10.5pt; margin: 4mm 0 1.6mm; }
svg { width: 100%; height: auto; display: block; }
svg text { font-family: "Liberation Sans", Arial, Helvetica, sans-serif; }
.wire { stroke: #1f3a5f; stroke-width: 2.3; fill: none; }
.pwire { stroke: #666; stroke-width: 1.6; fill: none; }
.sep { stroke: #999; stroke-width: 1; stroke-dasharray: 4 3; }
.dot { fill: #1f3a5f; }
.box { fill: #fff; stroke: #222; stroke-width: 1.6; }
.boxhead { fill: #e6ebf1; stroke: none; }
.mcu { fill: #f4f7f1; stroke: #1b4d2b; stroke-width: 2.4; }
.pad { fill: #c9a227; stroke: #6d5510; stroke-width: 1; }
.res, .part { fill: #fff; stroke: #222; stroke-width: 1.5; }
.led { stroke: #222; stroke-width: 1.3; }
.ledbar { stroke: #222; stroke-width: 2.6; }
.pill { stroke: none; }
.pilltext { fill: #fff; font-size: 12.5px; font-weight: 700; }
.restext { fill: #111; font-size: 12.5px; }
.title { fill: #111; font-size: 15px; font-weight: 700; }
.mcutitle { fill: #1b4d2b; font-size: 17px; font-weight: 700; }
.small { fill: #444; font-size: 11.5px; }
.pin { fill: #111; font-size: 14px; }
.mcupin { fill: #111; font-size: 14px; font-weight: 700; }
.legend { display: flex; flex-wrap: wrap; gap: 1.2mm 6mm; font-size: 8.4pt; color: #222; margin: 2mm 0 0; }
.legend span.k { display: inline-block; vertical-align: middle; margin-right: 1.4mm; }
.k.tag { color: #fff; font-weight: 700; font-size: 7.4pt; border-radius: 3mm; padding: .2mm 2mm; }
.k.w { width: 9mm; border-top: 2.3px solid #1f3a5f; }
.k.r { border: 1.4px solid #222; border-radius: .6mm; padding: 0 1.4mm; font-size: 7.6pt; }
.k.p { width: 2.4mm; height: 2.4mm; background: #c9a227; border: 1px solid #6d5510; }
.warn { border: 1.2pt solid #111; border-radius: 1.5mm; padding: 2mm 3mm; margin-top: 3mm; }
.warn h2 { margin: 0 0 1mm; }
ul { margin: 0; padding-left: 4.5mm; }
li { margin: .5mm 0; }
table { border-collapse: collapse; width: 100%; }
table.pins { font-size: 8.6pt; }
table.pins th { text-align: left; font-size: 8pt; text-transform: uppercase; letter-spacing: .03em; border-bottom: 1.2pt solid #111; padding: 1mm 1.5mm; }
table.pins td { padding: .7mm 1.5mm; border-bottom: .4pt solid #bbb; vertical-align: top; }
table.pins tr.first td { border-top: .9pt solid #555; }
table.pins td.comp { font-weight: 700; white-space: nowrap; }
table.pins td.to, table.pins td.mp { white-space: nowrap; }
.narrow svg { width: 80%; margin: 0 auto; }
table.pins td.note { color: #333; }
.chk { width: 7mm; text-align: center; }
.chk .box { display: inline-block; width: 3.2mm; height: 3.2mm; border: 1pt solid #111; vertical-align: middle; }
table.leds { font-size: 8.8pt; }
table.leds td { padding: .8mm 1.5mm; border-bottom: .4pt solid #bbb; vertical-align: top; }
table.leds td:first-child { white-space: nowrap; font-weight: 700; width: 26mm; }
.lamp { display: inline-block; width: 3mm; height: 3mm; border-radius: 50%; border: .8pt solid #222; margin-right: 1.6mm; vertical-align: -.3mm; }
footer { margin-top: 3mm; padding-top: 1.4mm; border-top: .5pt solid #888; font-size: 7.4pt; color: #555; display: flex; justify-content: space-between; }
@media screen { body { background: #777; } .sheet { background: #fff; margin: 8mm auto; padding: 11mm 12mm; width: 210mm; min-height: 297mm; } }
"""

LEGEND = (
    '<div class="legend">'
    '<span><span class="k w"></span>fio de sinal</span>'
    '<span><span class="k tag" style="background:#c62828">3V3</span>'
    '<span class="k tag" style="background:#e65100">5V</span>'
    '<span class="k tag" style="background:#1c1c1c">GND</span>ligar no pino de alimentação de mesmo nome da placa</span>'
    '<span><span class="k r">1 kΩ</span>resistor em série</span>'
    '<span><span class="k p"></span>pino</span>'
    "</div>"
)


def sheet(n, total, board, role, body):
    today = datetime.date.today().strftime("%d/%m/%Y")
    return (f'<section class="sheet"><header><div><h1>AirGuard IoT · Pinagem</h1><div class="board">{board}</div></div>'
            f'<div class="meta">{role}<br>Folha {n} de {total} · {today}</div></header>{body}'
            f'<footer><span>Desenho esquemático: siga o nome do pino impresso na placa, não a posição no desenho.</span>'
            f"<span>AirGuard IoT · folha {n}/{total}</span></footer></section>")


def build():
    esp_warn = bullets([
        "Os GPIO do ESP32 são de <b>3,3 V e não toleram 5 V</b>.",
        f"<b>MQ-135:</b> o AO chega a 5 V. Sempre pelo divisor (10 kΩ em série, 2× 10 kΩ de {gpio('MQ135_ADC')} ao GND). O DO não é ligado.",
        "<b>OLED, DHT11 e RYLR998 só em 3V3.</b> Só o MQ-135 vai no 5 V (VIN).",
        "<b>Antena do RYLR998 colocada antes de ligar.</b> Nunca transmitir sem antena.",
        "<b>GND comum</b> entre a placa e todos os módulos.",
        "Fonte de 5 V com 2 A ou mais. Uma porta USB de 500 mA fica no limite.",
    ])
    rp_warn = bullets([
        "<b>Soldar os pinos da RP2040-Zero.</b> Pinos só encaixados não fazem contato confiável.",
        "Os GPIO da RP2040 são de <b>3,3 V e não toleram 5 V</b>.",
        f"<b>LCD em 5 V:</b> por isso cada linha I2C ({gp('I2C_SDA')} e {gp('I2C_SCL')}) leva 10 kΩ ao GND (linhas em ~3,5 V). "
        "O recomendado é um conversor de nível BSS138 no lugar dos dois resistores.",
        "O 3V3 da placa alimenta só o rádio. O LCD vai direto no pino 5V; depois, ajustar o contraste no potenciômetro azul atrás do módulo.",
        "<b>Antena do RYLR998 colocada antes de ligar.</b> Nunca transmitir sem antena.",
    ])
    p1 = (draw_esp32() + LEGEND + f'<div class="warn"><h2>Antes de energizar</h2>{esp_warn}</div>')
    p2 = ("<h2>Ligações do sensor, fio a fio</h2>" + table(ESP32_ROWS)
          + "<h2>O que cada LED indica</h2>" + leds(ESP32_LEDS)
          + "<h2>Pinos do ESP32 que não devem ser usados para outras ligações</h2>"
          + bullets(["<b>GPIO 6 a 11:</b> ligados à memória flash.",
                     "<b>GPIO 1 e 3:</b> console USB.",
                     "<b>GPIO 0, 2, 5, 12 e 15:</b> pinos de <i>strapping</i>; um módulo neles pode impedir o boot.",
                     "<b>GPIO 14:</b> gera PWM durante o boot.",
                     "<b>GPIO 34 a 39:</b> só entrada, sem pull-up interno."]))
    p3 = ('<div class="narrow">' + draw_rp2040() + "</div>" + LEGEND + "<h2>Ligações do receptor, fio a fio</h2>" + table(RP2040_ROWS)
          + "<h2>O que cada LED indica</h2>" + leds(RP2040_LEDS)
          + f'<div class="warn"><h2>Antes de energizar</h2>{rp_warn}</div>')
    doc = ("<!doctype html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\"><title>AirGuard IoT · Pinagem</title>"
           f"<style>{CSS}</style></head><body>"
           + sheet(1, 3, "Sensor KITCHEN_01 · ESP32 DevKit (ESP32-WROOM-32)", "Diagrama de ligação", p1)
           + sheet(2, 3, "Sensor KITCHEN_01 · ESP32 DevKit (ESP32-WROOM-32)", "Lista de ligações", p2)
           + sheet(3, 3, "Receptor GATEWAY_01 · Waveshare RP2040-Zero", "Diagrama e lista de ligações", p3)
           + "</body></html>")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(doc, encoding="utf-8")
    return OUT


if __name__ == "__main__":
    print(build())
