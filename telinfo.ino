/*
 * Linky monitor
 * (c) Nicolas Provost, 2022-2023, dev@npsoft.fr
 * 
 * License: BSD 3-clause
 *
 * D3-D9: lcd
 *   NOTE D10 was backlight enable/disable with pull-up.
 *        Was disconnected on lcd board to use D10 as
 *        chip select on SD shield => move D10 to D3 on
 *        the lcd board.
 * UART1: linky output 
 * 
 * v0.60: add keyboard (on A0)
 * v0.50: initial version
 */
 
#include <LiquidCrystal.h>
#include <SoftwareSerial.h>
#include <avr/pgmspace.h>

#define VERSION "Telinfo v0.60"
#define BACKLIGHT_PIN 3

typedef char lcd_line_t[17];

/* status */
typedef enum
{
  STATE_LCD_ON = 1,
} state_t;

/* data received from Linky (tags) */
typedef enum
{
  LD_DATE = 1,
  LD_HEURE = 2,
  LD_BASE = 4, /* compteur W */
  LD_IINST = 8, /* intensite instantanee */
  LD_IMAX = 0x10, /* intensite max */
  LD_PAPP = 0x20, /* puissance apparente */
  LD_ADCO = 0x40, /* addresse compteur */
  LD_OPTARIF = 0x80, /* option tarifaire */
  LD_ISOUSC = 0x100, /* intensite souscrite en A */
  LD_MOTDETAT = 0x200, /* mot d'etat */
  LD_PTEC = 0x400, /* periode tarifaire en cours */
  LD_HHPHC = 0x800, /* heures creuses-pleines */
  LD_END = 0x1000,
  LD_OK = 0x10000,
  LD_ERROR = 0x20000,
  LD_UNKNOWN = 0x40000,
} linky_field_t;

/* describes one data received */
typedef struct
{
  linky_field_t code;
  const char* name;
  const char* desc;
  const char* unit;
  char* data;
  int timestamp;
} linky_def_t;


/* global variables */
lcd_line_t lcd_line0 = { 0 }; /* lcd line 0 */
lcd_line_t lcd_line1 = { 0 }; /* lcd line 1 */
char* line0 = lcd_line0; /* current lcd line 0 */
char* line1 = lcd_line1; /* current lcd line 1 */
int state = 0; /* current program state (state_t) */
LiquidCrystal lcd (8, 9, 4, 5, 6, 7);  
int temperature;
int chk; /* checksum */
linky_field_t fields_read;

/* description of received data:
 */
linky_def_t fields[] =
{
  { LD_DATE, "DATE", "Date", "", "xx/xx/xx", 0 },  
  { LD_HEURE, "HEURE", "Heure", "", "xx:xx", 0 },
  { LD_BASE, "BASE", "Index", "Wh", "123456789", 0 },
  { LD_IINST, "IINST", "I instant.", "A", "123", 0 },
  { LD_IMAX, "IMAX", "Intensite max","A", "123", 0 },
  { LD_PAPP, "PAPP", "Puissance app.", "VA", "12345", 0 },
  { LD_PTEC, "PTEC", "Periode tarif", "", "1234", 0 },
  { LD_ADCO, "ADCO", "Addresse compt", "", "123456789ABC", 0 },
  { LD_OPTARIF, "OPTARIF", "Option tarif", "", "1234", 0 },
  { LD_ISOUSC, "ISOUSC", "I souscrite", "A", "12", 0 },
  { LD_HHPHC, "HHPHC", "Heures creuses", "", "1", 0 },
  { LD_MOTDETAT, "MOTDETAT", "Etat", "", "123456", 0 },
  { LD_END, NULL, NULL, NULL, 0 }
};


/* ====================================================================
   = LCD                                                              =
   ==================================================================== */ 
#define btnRIGHT 0
#define btnUP 1
#define btnDOWN 2
#define btnLEFT 3
#define btnSELECT 4
#define btnNONE 5
#define BUTTONS_CHANNEL 0 /* A0 */

/* read the buttons state on LCD board */
static uint8_t read_buttons (bool wait_release)
{
  /* buttons on A0 */
  uint16_t adc_key_in = analogRead (BUTTONS_CHANNEL);
  uint8_t btn;
  
  if (adc_key_in > 1000)
    return btnNONE;
  
  if (adc_key_in < 50)
    btn = btnRIGHT;
  else if (adc_key_in < 150)
    btn = btnUP;
  else if (adc_key_in < 400)
    btn = btnDOWN;
  else if (adc_key_in < 600)
    btn = btnLEFT;
  else if (adc_key_in < 800)
    btn = btnSELECT;
  else
    return btnNONE;
  if (wait_release)
  {
    while (analogRead (BUTTONS_CHANNEL) < 1000)
    { }
  }
  return btn;
}

/* ====================================================================
   = LCD                                                              =
   ==================================================================== */ 
/* LCD line clear: 0/1/2 for both */
static void line_clear (int n)
{
  int i;
  
  if ((n == 0) || (n == 2))
  {
    for (i = 0; i <= 16; i++)
      line0[i] = ' ';
     line0[16] = 0;
  }
  if ((n == 1) || (n == 2))
  {
    for (i = 0; i <= 16; i++)
      line1[i] = ' ';
    line1[16] = 0;
  }
}

/* line set */
static void line_set (int line, int n, char c)
{
  if ((n >= 0) && (n < 16) && (line >= 0) && (line <= 1))
  {
    if (line == 0)
      line0[n] = c;
    else
      line1[n] = c;
  }
}

/* line print */
static void line_print (int line, const char* s)
{
  char* l = (line == 0) ? line0 : line1;
  int i;

  for (i = 0; (i < 16) && (s[i] != 0); i++)
    l[i] = s[i];
  for ( ; i < 16; i++)
    l[i] = ' ';
  l[16] = 0;
}

/* print integer 'n' in line 'line' starting at 'index' */
static void line_print (int line, int n, int index)
{
  char* l = (line == 0) ? line0 : line1;
  int i;
  bool neg = false;
  char nb[16] = { 0 };
  int iMin;

  if (index > 15)
    return;
  if (n < 0)
  {
    neg = true;
    n = -n;
  }
  if (n == 0)
  {
    nb[15] = '0';
    i = 14;
  }
  else
  {
    i = 15;
    while (n > 0)
    {
      nb[i--] = (n % 10) + '0';
      n /= 10;
    }
  }
  iMin = ++i;
  i = index;
  if (neg)
    l[i++] = '-';
  while (iMin <= 15)
    l[i++] = nb[iMin++];
  l[16] = 0;
}

/* redraw line */
static void line_show (int line)
{
  if ((line == 0) || (line == 2))
  {
    lcd.setCursor (0, 0);
    lcd.print (line0);
  }
  if ((line == 1) || (line == 2))
  {
    lcd.setCursor (0, 1);
    lcd.print (line1);
  }
}

/* backlight on or off */
static void backlight (bool enable)
{
  if (enable)
    state |= STATE_LCD_ON;
  else
    state &= ~STATE_LCD_ON;
  digitalWrite (BACKLIGHT_PIN, enable ? HIGH : LOW);
}

/* ===========================================================
   = Program checksum                                        =
   =========================================================== */
static void prg_checksum ()
{
  static const char hex[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
              '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
  uint32_t sum = 0;
  uint16_t i;
  int j;
  unsigned char c;

  line_print (1, "fw:");

  for (i = 0; i < 32768; i++)
    sum += (unsigned char) pgm_read_byte (i);
  for (i = 0, j = 7+3; i < 4; i++, sum >>= 8)
  {
    c = sum & 0xFF;
    line_set (1, j--, hex[c % 16]);
    line_set (1, j--, hex[c / 16]);
  }
  line_show (1);
}

static void show_fw_version ()
{
  line_clear (2);  
  line_print (0, VERSION);
  line_show (0);
  prg_checksum ();
}

/*=========================================================
  = Internal temperature sensor                           =
  ========================================================= */
/* read temperature in celsius
 * return the temperature * 10
 * calibration: using 5v ref, read ADC=75 at 20.5°: 366 mv
 * Datasheet: sensitivity ~ 1 mv/°
 * -45° ~ 0.242v / +25° ~ 0.314v / +85° ~ 0.380v
 * 
 * source: arduino.cc
 */
static int get_temp10 ()
{
  unsigned int wADC;
  double t;

  // The internal temperature has to be used
  // with the internal reference of 1.1V.
  // Channel 8 can not be selected with
  // the analogRead function yet.

  // Set the internal reference and mux.
  ADMUX = (_BV(REFS1) | _BV(REFS0) | _BV(MUX3));
  ADCSRA |= _BV(ADEN);  // enable the ADC

  delay (20);            // wait for voltages to become stable.

  ADCSRA |= _BV(ADSC);  // Start the ADC

  // Detect end-of-conversion
  while (bit_is_set (ADCSRA,ADSC));

  // Reading register "ADCW" takes care of how to read ADCL and ADCH.
  wADC = ADCW;

  // The offset of 324.31 could be wrong. It is just an indication.
  t = (wADC - 328) / 1.22;

  // The returned temperature is in degrees Celsius.
  return int (t * 10);
}

/* display temperature on given line */
static void show_temp (int line)
{
  /* show temperature */
  line_print (line, "temp=xx.x C");
  line_print (line, temperature / 10, 5);
  line_print (line, temperature % 10, 8);
  line_show (line);
}
 
/* ====================================================
   = Hardware setup                                   =
   ==================================================== */
void setup ()
{
  /* init LCD and show firmware version */
  lcd.begin (16, 2);
  lcd.clear ();
  pinMode (BACKLIGHT_PIN, OUTPUT);
  backlight (true);
  show_fw_version ();
  delay (1500);

  /* get current temperature */
  temperature = get_temp10 ();

  /* UART, Linky output */
  Serial.begin (1200, SERIAL_7E1);    
}

/* ==========================================================
   = Linky data                                             =
   ========================================================== */
#define STX 2
#define ETX 3
#define EOT 4
#define LF 0xA
#define SEP 32
#define CR 0xD

/* read a byte with timeout (-1 on timeout) */
static int read_byte ()
{
  int i;

  for (i = 0; i < 100; i++)
  {
    if (Serial.available () > 0)
      return Serial.read ();
    else
      delay (1);
  }
  return -1;
}

/* read tag (up to 8 chars) ended with SEP 
 * return LD_xxx */
static linky_field_t read_etiq (linky_def_t** field)
{
  int i;
  int b;
  char etiq[10]; /* current tag */

  *field = NULL;
  for (i = 0; ; i++)
  {
    b = read_byte ();
    if (b < 0)
      break;
    chk += b;
    
    if (b == SEP)
    {
      if (i < 10)
      {
        etiq[i] = 0;
        for (i = 0; fields[i].name; i++)
        {
          if (strcmp (fields[i].name, etiq) == 0)
          {
            *field = &fields[i];
            return fields[i].code;
          }
        } 
      }
      /*line_clear(0);
      line_print (0, etiq);
      line_show (0);
      delay (700);*/
      return LD_UNKNOWN;
    }
    else if (i < 9)
      etiq[i] = b;
  }
  return LD_ERROR;
}

/* read the content of the field until SEP */
static void read_data (linky_def_t* field)
{
  int i;
  int b;
  int maxlen = 15;
  char* p = NULL;

  /* some fields are not recorded */
  if (field)
    p = field->data;

  /* read up to maxlen+1 (including SEP) chars */
  for (i = 0; ; i++)
  {
    b = read_byte ();
    chk += b;
    if (b == SEP)
      break;
    else if (p && (i < maxlen))
      *p++ = b;
  }
  if (p)
  {
    *p = 0;
    fields_read |= field->code;
  }
}

/* return LD_OK if data was read */
static linky_field_t read_linky ()
{
  int i;
  int b = 0;
  int lchk;
  linky_def_t* field;

  fields_read = 0;

  /* wait for STX */
  line_print (0, VERSION);
  line_show (0);
  line_print (1, "waiting");
  line_show (1);
  while (b != STX)
    b = read_byte ();
  line_print (1, "reading");
  line_show (1);
  while (1)
  {
    b = read_byte ();
    switch (b)
    {
      case ETX:
        return LD_OK;
      default:
        return LD_ERROR; 
      case LF: /* start of group of info */
        chk = 0;
        read_etiq (&field);
        read_data (field);
        lchk = read_byte ();
        if (read_byte () != CR)
          return LD_ERROR;
        break;
    }
  };
}

/* ==========================================================
   = Main loop                                              =
   ========================================================== */
void loop ()
{
  uint8_t buttons;
  uint8_t do_read = 1; 
  uint8_t current_field = 0; 
  char buf[16];

  while(true)
  {
    if (do_read)
    {
      while (read_linky () != LD_OK)
        delay (250);
      do_read = 0;
    }

    /* show current field */
    line_clear (2);
    line_print (0, fields[current_field].desc);
    memset (buf, 0, 16);
    if (fields_read & fields[current_field].code)
      strcat (buf, fields[current_field].data);
    else
      strcat (buf, "?");
    strcat (buf, " ");
    strcat (buf, fields[current_field].unit);
    line_print (1, buf);
    line_show (2);

    /* buttons */
    buttons = read_buttons (true);
    switch (buttons)
    {
      case btnNONE:
      default:
        break;
      case btnSELECT:
        do_read = 1;
        break;
      case btnUP:
        if (current_field == 0)
        {
          while (fields[++current_field].code != LD_END) { };
        }
        current_field--;
        break;
      case btnDOWN:
        if (fields[current_field+1].code == LD_END)
          current_field = 0;
        else
          current_field++;
        break;
    }
    
    delay (250);
  }
}
