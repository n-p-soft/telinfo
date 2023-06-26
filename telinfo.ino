/*
 * Linky monitor
 * (c) Nicolas Provost, 08/2022 dev@npsoft.fr
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
 * v0.50: initial version
 */
 
#include <LiquidCrystal.h>
#include <SoftwareSerial.h>
#include <avr/pgmspace.h>

#define VERSION "Telinfo v0.50"
#define BACKLIGHT_PIN 3

typedef char lcd_line_t[17];

/* status */
typedef enum
{
  STATE_LCD_ON = 1,
  STATE_STX = 2, /* got STX */
  STATE_GI = 4, /* in info group */
} state_t;

/* data received from Linky */
typedef enum
{
  LD_ERROR = 0,
  LD_UNKNOWN = 1,
  LD_ADCO = 2,
  LD_BASE = 4,
  LD_MOTDETAT = 8,
} linky_field_t;

typedef struct
{
  linky_field_t code;
  const char* name;
  const char* unit;
  int len;
  int timestamp;
} linky_def_t;

const linky_def_t fields[] =
{
  { LD_BASE, "BASE", "Wh", 9, 0 },
  { LD_ADCO, "ADCO", "", 12, 0 },
  { LD_MOTDETAT, "MOTDETAT", "", 6, 0 },
  { LD_UNKNOWN, NULL, NULL, 0, 0 }
};

/* Linky data */
typedef struct linky_data_t 
{
  int fields; /* fields that are set below */
  char base[10];
  char adco[13];
  char motdetat[7];
} linky_data_t;

/* global variables */
lcd_line_t lcd_line0 = { 0 }; /* lcd line 0 */
lcd_line_t lcd_line1 = { 0 }; /* lcd line 1 */
char* line0 = lcd_line0; /* current lcd line 0 */
char* line1 = lcd_line1; /* current lcd line 1 */
int state = 0; /* current program state (state_t) */
LiquidCrystal lcd (8, 9, 4, 5, 6, 7);  
int temperature;
char etiq[9]; /* current tag */
int chk; /* checksum */
linky_data_t ldata;


/* ====================================================================
   = LCD                                                              =
   ==================================================================== */ 
#define btnRIGHT 0
#define btnUP 1
#define btnDOWN 2
#define btnLEFT 3
#define btnSELECT 4
#define btnNONE 5

/* read the buttons state on LCD board */
static uint8_t read_buttons (bool wait_release)
{
  /* buttons on A0 */
  /*uint16_t adc_key_in = analogRead (BUTTONS_CHANNEL);
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
  return btn;*/
  return 0;
}

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
static int read_etiq (const linky_def_t** field)
{
  int i;
  int b;
  
  *field = NULL;
  for (i = 0; i < 9; i++)
  {
    b = read_byte ();
    if (b < 0)
      break;
    chk += b;
    /*line_print (0, b, 0);
    line_show (0);
    delay (700);*/
    if (b == SEP)
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
      return LD_UNKNOWN;
    }
    else
      etiq[i] = b;
  }
  return LD_ERROR;
}

/* return the field of ldata associated to code */
static char* field_data (linky_field_t code)
{
  switch (code)
  {
    case LD_ADCO:
      return ldata.adco;
    case LD_BASE:
      return ldata.base;
    case LD_MOTDETAT:
      return ldata.motdetat;
    default:
      return NULL;
  }
}

/* read the content of the field until SEP */
static int read_data (const linky_def_t* field)
{
  int i;
  int b;
  char* p;
  int maxlen;
  
  if (field)
  {
    maxlen = field->len;
    p = field_data (field->code);
  }
  else
  {
    maxlen = 24;
    p = NULL;
  }
  /* read up to maxlen+1 (SEP) chars */
  for (i = 0; i <= maxlen; i++)
  {
    b = read_byte ();
    if (b < 0)
      return 0;
    chk += b;
    if (b == SEP)
    {
      if (p)
      {
        *p = 0;
        ldata.fields |= field->code;
      }
      return 1;
    }
    else if (p)
      *p++ = b;
  }
  return 0;
}

/* return 1 if data was read, 0 if no STX, -1 on error */
static int read_linky ()
{
  int i;
  int b = 0;
  int lchk;
  const linky_def_t* field;

  memset (&ldata, 0, sizeof (linky_data_t));

  /* wait for STX */
  line_print (0, VERSION);
  line_show (0);
  line_print (1, "...");
  line_show (1);
  state &= ~ (STATE_STX | STATE_GI);
  while (b != STX)
    b = read_byte ();
  line_print (1, "reading");
  line_show (1);
  state |= STATE_STX;
  while (1)
  {
    b = read_byte ();
    switch (b)
    {
      case ETX:
        return 1;
      case EOT:
        return 0;
      default:
        return -1; 
      case LF: /* start of group of info */
        if (state & STATE_GI)
          return -1;
        state |= STATE_GI;
        chk = 0;
        if (read_etiq (&field) == LD_ERROR)
          return -1;
        if ( ! read_data (field))
          return -1;
        lchk = read_byte ();
        if (read_byte () != CR)
          return -1;
        //if (lchk != ((chk & 0x3f) + 0x20))
        //  return -1;
        state -= STATE_GI;
        break;
    }
  };
}

static int show_linky ()
{
  int i;
  char* p;

  for (i = 0; fields[i].name; i++)
  {
    if ( ! (ldata.fields & fields[i].code))
      continue;
    p = field_data (fields[i].code);
    if ((p == NULL) || (*p == 0))
      continue;
    line_clear (2);
    line_print (0, fields[i].name);
    line_print (1, p);
    line_show (2);
    delay (1400);
  }
}

/* ==========================================================
   = Main loop                                              =
   ========================================================== */
void loop ()
{
  switch (read_linky ())
  {
    case 1:
      show_linky ();
      break;
    case -1:
      line_print (1, "error");
      line_show (1);
      break;
  }
  delay (250);
}
