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
 * v0.70: 10/05/23
 *        passage en mode standard (define MODE_HISTO sinon)
 *        fw chk: 0052A571
 * v0.60: add keyboard (on A0)
 * v0.50: initial version
 */
 
#include <LiquidCrystal.h>
#include <SoftwareSerial.h>
#include <avr/pgmspace.h>

#define VERSION "Telinfo v0.70"
#define BACKLIGHT_PIN 3

/* status */
typedef enum
{
  STATE_LCD_ON = 1,
  STATE_STX = 2, /* got STX */
} state_t;

/* describes one data received */
typedef struct
{
  uint8_t code;
  const char* name;
  const char* desc;
  const char* unit;
  char* data;
  uint8_t maxlen;
} linky_def_t;

/* data received from Linky (tags) */
typedef enum
{
  LD_DATE = 0,
  LD_HEURE, /* pseudo-field: time */
  LD_INDEX, /* compteur Wh energie totale */
  LD_IINST, /* intensite instantanee */
  LD_MAX, /* intensite ou puissance max */
  LD_MAX1, /* puissance max n-1 */
  LD_PAPP, /* puissance apparente */
  #ifdef MODE_HISTO
  LD_ADCO, /* addresse compteur */
  #else
  LD_ADSC, /* address secondaire */
  #endif
  #ifdef MODE_HISTO
  LD_OPTARIF, /* option tarifaire */
  LD_ISOUSC, /* intensite souscrite en A */
  #endif
  LD_HHPHC, /* heures creuses-pleines */
  LD_MOTDETAT, /* mot d'etat */
  #ifdef MODE_HISTO
  LD_PTEC, /* periode tarifaire en cours */
  #endif
  #ifndef MODE_HISTO
  LD_NGTF, /* nom calendrier tarifaire */
  LD_LTARF, /* nom tarif fournisseur */
  LD_EASD01, /* energie soutiree, distributeur 1 */
  LD_EAIT, /* energie injectee totale */
  LD_SINSTI, /* puissance app. instantanee injectee */
  LD_SMAXIN, /* puissance app. max. injectee */
  LD_SMAXIN1, /* puissance app. max. injectee n-1 */
  LD_ERQ1, /* energie reactive totale Q1 */
  LD_ERQ2, /* energie reactive totale Q2 */
  LD_ERQ3, /* energie reactive totale Q3 */
  LD_ERQ4, /* energie reactive totale Q4 */
  LD_URMS1, /* tension efficate phase 1 */
  LD_UMOY1, /* tension moyenne phase 1 */
  LD_PREF, /* puissance app. de reference */
  LD_PCOUP, /* puissance app. coupure */
  LD_CCASN, /* point N courbe puiss act. soutiree */
  LD_CCASN1, /* point N-1 courbe puiss act. soutiree */
  LD_CCAIN, /* point N courbe puiss act. injectee */
  LD_CCAIN1, /* point N-1 courbe puiss act. injectee */
  LD_DPM1, /* debut pointe mobile 1 */
  LD_FPM1, /* fin pointe mobile 1 */
  LD_DPM2, /* debut pointe mobile 2 */
  LD_FPM2, /* fin pointe mobile 2 */
  LD_DPM3, /* debut pointe mobile 3 */
  LD_FPM3, /* fin pointe mobile 3 */
  LD_MSG1, /* message */
  LD_MSG2, /* message court */
  LD_PRM, /* = point de livraison */
  LD_RELAIS,
  LD_NJOURF, /* numero jour en cours calendrier fournisseur */
  LD_NJOURF1, /* numero prochain jour calendrier fournisseur */
  #endif
  LD_MASK=0x3f, /* mask and end of list */
  LD_HORODATE = 0x40,
  LD_READ = 0x80,
} telinfo_code_t;

typedef enum
{
  TL_ERR_NONE = 0,
  TL_ERR = 0x1, /* unknown */
  TL_ERR_TRAME = 0x2, /* bad trame byte */
  TL_ERR_HORO = 0x3, /* bad horodata */
  TL_ERR_TAG = 0x4, /* bad field name */
  TL_ERR_DATA = 0x5, /* error reading field's data */
} telinfo_err_t;

/* description of received data 
 * Same order than previous enum LD_xxx */
linky_def_t fields[] =
{
  { LD_DATE | LD_HORODATE, "DATE", "Date", NULL, "aa/mm/jj", 8, },  
  { LD_HEURE | LD_HORODATE, "DATE", "Heure", NULL, "hh:mm:ss S", 10, },  
  #ifdef MODE_HISTO
  { LD_INDEX, "BASE", "Index", "Wh", "123456789", 9, },
  #else
  { LD_INDEX, "EAST", "Index", "Wh", "123456789", 9, },
  #endif
  #ifdef MODE_HISTO
  { LD_IINST, "IINST", "I instant.", "A", "123", 3, },
  #else
  { LD_IINST, "IRMS1", "Courant phase 1", "A", "123", 3, },
  #endif
  #ifdef MODE_HISTO
  { LD_MAX, "IMAX", "Intensite max","A", "123", 3, },
  #else
  { LD_MAX | LD_HORODATE, "SMAXSN", "Puissance max","VA", "12345", 5, },
  #endif
  { LD_MAX1 | LD_HORODATE, "SMAXSN-1", "Puis. max n-1","VA", "12345", 5, },
  #ifdef MODE_HISTO
  { LD_PAPP, "PAPP", "Puissance app.", "VA", "12345", 5, },
  #else
  { LD_PAPP, "SINSTS", "Puissance app.", "VA", "12345", 5, },
  #endif
  #ifdef MODE_HISTO
  { LD_ADCO, "ADCO", "Addresse compt", NULL, "123456789ABC", 12, },
  #else
  { LD_ADSC, "ADSC", "Addresse compt", NULL, "123456789ABC", 12, },
  #endif
  #ifdef MODE_HISTO
  { LD_OPTARIF, "OPTARIF", "Option tarif", NULL, "1234", 4, },
  { LD_ISOUSC, "ISOUSC", "I souscrite", "A", "12", 2, },
  #endif
  #ifdef MODE_HISTO
  { LD_HHPHC, "HHPHC", "Heures creuses", NULL, "1", 1, },
  #else
  { LD_HHPHC, "NTARF", "N. index tarif", NULL, "12", 2, },
  #endif
  #ifdef MODE_HISTO
  { LD_MOTDETAT, "MOTDETAT", "Etat", NULL, "123456", 6, },
  #else
  { LD_MOTDETAT, "STGE", "Etat", NULL, "12345678", 8, },
  #endif
  #ifdef MODE_HISTO
  { LD_PTEC, "PTEC", "Periode tarif", NULL, "1234", 4, },
  #endif
  #ifndef MODE_HISTO
  { LD_NGTF, "NGTF", "Nom calendrier", NULL, "0123456789ABCDEF", 16, },
  { LD_LTARF, "LTARF", "Lib. tarif", NULL, "0123456789ABCDEF", 16, },
  { LD_EASD01, "EADS01", "Distributeur 1", "Wh", "123456789", 9, }, 
  { LD_EAIT, "EAIT", "Total injecte", "Wh", "123456789", 9, },
  { LD_SINSTI, "SINSTI", "P inst. inj.", "VA", "12345", 5, },
  { LD_SMAXIN | LD_HORODATE, "SMAXIN", "P max. inj.", "VA", "12345", 5, },
  { LD_SMAXIN1 | LD_HORODATE, "SMAXIN-1", "P max. inj. n-1", "VA", "12345", 5, },
  { LD_ERQ1, "ERQ1", "En. react Q1", "VArh", "123456789", 9, },
  { LD_ERQ2, "ERQ2", "En. react Q2", "VArh", "123456789", 9, },
  { LD_ERQ3, "ERQ3", "En. react Q3", "VArh", "123456789", 9, },
  { LD_ERQ4, "ERQ4", "En. react Q4", "VArh", "123456789", 9, },
  { LD_URMS1, "URMS1", "Tension eff ph 1", "V", "123", 3, },
  { LD_UMOY1 | LD_HORODATE, "UMOY1", "Tension moy ph 1", "V", "123", 3, },
  { LD_PREF, "PREF", "Puis. app. ref", "kVA", "12", 2, },
  { LD_PCOUP, "PCOUP", "Puis. app. coup", "kVA", "12", 2, },
  { LD_CCASN | LD_HORODATE, "CCASN", "Pt N puis sout", "W", "12345", 5, },
  { LD_CCASN1 | LD_HORODATE, "CCASN-1", "Pt N-1 puis sout", "W", "12345", 5, },
  { LD_CCAIN | LD_HORODATE, "CCAIN", "Pt N puis inj", "W", "12345", 5, },
  { LD_CCAIN1 | LD_HORODATE, "CCAIN-1", "Pt N-1 puis inj", "W", "12345", 5, },
  { LD_DPM1 | LD_HORODATE, "DPM1", "Debut pointe 1", NULL, "12", 2, },
  { LD_FPM1 | LD_HORODATE, "FPM1", "Fin pointe 1", NULL, "12", 2, },
  { LD_DPM2 | LD_HORODATE, "DPM2", "Debut pointe 2", NULL, "12", 2, },
  { LD_FPM2 | LD_HORODATE, "FPM2", "Fin pointe 2", NULL, "12", 2, },
  { LD_DPM3 | LD_HORODATE, "DPM3", "Debut pointe 3", NULL, "12", 2, },
  { LD_FPM3 | LD_HORODATE, "FPM3", "Fin pointe 3", NULL, "12", 2, },
  { LD_MSG1, "MSG1", "Message", NULL, "012345678901234567890123456789AB", 32, },
  { LD_MSG2, "MSG2", "Message court", NULL, "0123456789ABCDEF", 16, },
  { LD_PRM, "PRM", "PRM", NULL, "01234567891234", 14, },
  { LD_RELAIS, "RELAIS", "Relais", NULL, "123", 3, },
  { LD_NJOURF, "NJOURF", "Jour courant", NULL, "12", 2, },
  { LD_NJOURF1, "NJOURF+1", "Prochain jour", NULL, "12", 2, },
  #endif
  { LD_MASK, NULL, NULL, NULL, NULL, 0 }
};

/* maximum data in a field is for now 98 */
#define MAXDATA 98

/* max field name len */
#define MAXTAG 8

/* horodata len */
#define HLEN 13

/* max len of a dataset */
#define MAXINFO (1+MAXTAG+1+HLEN+1+MAXDATA+1+1+1)

/* global variables */
char line0[17]; /* current lcd line 0 */
char line1[17]; /* current lcd line 1 */
int state = 0; /* current program state (state_t) */
LiquidCrystal lcd (8, 9, 4, 5, 6, 7);  
int chk; /* checksum */
telinfo_err_t err = TL_ERR_NONE; /* last error code */
char info[MAXINFO+1];
char* pinfo; /* current pointer in info */


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
    for (i = 0; i < 16; i++)
      line0[i] = ' ';
     line0[16] = 0;
  }
  if ((n == 1) || (n == 2))
  {
    for (i = 0; i < 16; i++)
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
    state &= ~ STATE_LCD_ON;
  digitalWrite (BACKLIGHT_PIN, enable ? HIGH : LOW);
}

static void error (telinfo_err_t ld)
{
  const char* msg;

  switch (ld)
  {
    case TL_ERR_NONE:
      msg = "no error";
      break;
    case TL_ERR_TRAME:
      msg = "cell error";
      break;
    case TL_ERR_DATA:
      msg = "data error";
      break;
    case TL_ERR_TAG:
      msg = "bad field name";
      break;
    case TL_ERR_HORO:
      msg = "bad horodata";
      break;
    default:
      msg = "unknown error";
      break;
  }
  line_print (1, msg);
  line_show (1);
  delay (2000);
}

static void debug (const char* s)
{
  line_clear(0);
  line_print (0, s);
  line_show (0);
  delay (700);
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

  /* UART, Linky output */
  #ifdef MODE_HISTO
  Serial.begin (1200, SERIAL_7E1);    
  #else
  Serial.begin (9600, SERIAL_7E1);    
  #endif
}

/* ==========================================================
   = Linky data                                             =
   ========================================================== */
#define STX 2
#define ETX 3
#define EOT 4
#define LF 0xA
#ifdef MODE_HISTO
#define SEP 32
#else
#define SEP 0x9 /* HT tab */
#endif
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
  return EOT;
}

/* read horodata: SAAMMJJHHMMss (S == E/H) */
static bool read_horodata (linky_def_t* f)
{
  uint8_t i;
  int b;
  char* horodata = pinfo;

  for (i = 0; i < 13; i++)
  {
    b = *pinfo++;
    chk += b;
    if (i == 0)
    {
      if (b != 'H' && b != 'E' && b != 'h' &&
          b != 'e' && b != ' ')
      {
        err = TL_ERR_HORO;
        return false;
      }
    }
    else if (b < '0' || b > '9')
    {
      err = TL_ERR_HORO;
      return false;
    }
  }
  b = *pinfo++;
  chk += b;
  if (b != SEP)
  {
    err = TL_ERR_HORO;
    return false;
  }

  /* decode DATE field */
  if (((f->code & LD_MASK) == LD_DATE) ||
      ((f->code & LD_MASK) == LD_HEURE))
  {
     fields[LD_DATE].data[0] = horodata[5];
     fields[LD_DATE].data[1] = horodata[6];
     fields[LD_DATE].data[2] = '/';
     fields[LD_DATE].data[3] = horodata[3];
     fields[LD_DATE].data[4] = horodata[4];
     fields[LD_DATE].data[5] = '/';
     fields[LD_DATE].data[6] = horodata[1];
     fields[LD_DATE].data[7] = horodata[2];
     fields[LD_HEURE].data[0] = horodata[7];
     fields[LD_HEURE].data[1] = horodata[8];
     fields[LD_HEURE].data[2] = ':';
     fields[LD_HEURE].data[3] = horodata[9];
     fields[LD_HEURE].data[4] = horodata[10];
     fields[LD_HEURE].data[5] = ':';
     fields[LD_HEURE].data[6] = horodata[11];
     fields[LD_HEURE].data[7] = horodata[12];
     fields[LD_HEURE].data[8] = ' ';
     fields[LD_HEURE].data[9] = horodata[0]; 
  }
  return true;
}

/* read the content of the field until SEP. If 'field' is NULL,
 * only read data.
 */
static bool read_data (linky_def_t* field)
{
  int i;
  int b;
  char* p;
  
  /* some fields are not recorded (special case: LD_DATE
   * is already filled using horodata
   */
  if (field == NULL)
    return true;
  else if (((field->code & LD_MASK) == LD_DATE) ||
           ((field->code & LD_MASK) == LD_HEURE))
  {
    field->code |= LD_READ;
    return true;
  }
  else
      p = field->data;
 
  /* read and store up to maxlen chars + SEP */
  for (i = 0; i < field->maxlen; i++)
    field->data[i] = 0;
  for (i = 0; i < field->maxlen + 1; i++)
  {
    b = *pinfo++;
    chk += b;
    if (b == SEP)
    {
      field->code |= LD_READ;
      return true;
    }
    else
      *p++ = b;
  }
  err = TL_ERR_DATA;
  return false;
}

/* read the data */
static bool read_dataset ()
{
  int i;
  int b;

  err = TL_ERR_NONE;
  chk = 0;

  /* read the whole info group */
  for (i = 0; i < MAXINFO; i++)
  {
    b = read_byte ();
    if (b == EOT)
    {
      err = TL_ERR_TRAME;
      return false;
    }
    else if (b == CR)
    {
      info[i] = 0;
      return true;
    }
    else
      info[i] = b;
  }
  err = TL_ERR_TRAME;
  return false;
}

/* see if current dataset (info) is related to field #nfield */
static bool match_dataset (int nfield)
{
  int i;

  pinfo = info;
  for (i = 0; ; i++, pinfo++)
  {
    if (*pinfo == 0)
      return false;
    else if (fields[nfield].name[i] == 0) 
    {
      if (*pinfo != SEP)
        return false;
      else
        break;
    }
    else if (*pinfo != fields[nfield].name[i])
      return false;
  }
  if (fields[nfield].name[i] != 0)
    return false;
  else
    pinfo++;

  /* horodata ? */
  if (fields[nfield].code & LD_HORODATE)
  {
    if ( ! read_horodata (&fields[nfield]))
      return false;
  }

  /* read data */
  if ( ! read_data (&fields[nfield]))
    return false;

  return true;
}

/* return true if data for given field was successfully read */
static bool read_linky (int nfield, bool quiet)
{
  int b = 0;

  err = TL_ERR_NONE;
  fields[nfield].code &= ~ LD_READ;
  while (1)
  { 
    /* wait for STX */
    line_print (1, "reading");
    line_show (1);
    while (b != STX)
      b = read_byte ();
    state |= STATE_STX;

    /* read all information groups until ETX */
    while (state & STATE_STX)
    {
      b = read_byte ();
      switch (b)
      {
        case ETX: /* end of trame, not found */
          state -= STATE_STX;
          return false;
        default:
          error (TL_ERR_TRAME);
          state -= STATE_STX;
          break;
        case LF: /* start of group */
          if (read_dataset ())
          {
            if (match_dataset (nfield))
              return true;
          }
          else
            state -= STATE_STX;
          if (err != TL_ERR_NONE)
          {
            state -= STATE_STX;
            if ( ! quiet)
              error (err);
          }          
          break;
      }
    }
    /* try again to read a full trame */
  };
}

/* display field state */
static void display_field (linky_def_t* f)
{
  const char* p;
  uint8_t i;
  
  line_clear (2);
  if (f->code & LD_READ)
  {
    if ((f->code & LD_MASK) == LD_MSG1)
    {
      for (i = 0; i < 16; i++)
        line_set (0, i, f->data[i]);
      for (i = 0; i < 16; i++)
        line_set (1, i, f->data[i+16]);
    }
    else
    {
      line_print (0, f->desc);
      for (i = 0; i < f->maxlen; i++)
        line_set (1, i, f->data[i]);
      if (f->unit)
      {
        line_set (1, i++, ' ');
        for (p = f->unit; *p; p++)
          line_set (1, i++, *p);
      }
    }
  }
  else
  {
    line_print (0, f->desc);
    line_print (1, "?");
  }
  line_show (2);
}

/* ==========================================================
   = Main loop                                              =
   ========================================================== */
void loop ()
{
  uint8_t buttons;
  int8_t i;
  int8_t current_field = 0; 

  line_clear (2);
  line_print (0, fields[current_field].desc);
  line_print (1, "?");
  line_show (2);

  while (true)
  {
    /* buttons */
    buttons = read_buttons (true);
    if (buttons == btnNONE)
    {
      delay (200);
      continue;
    }
    switch (buttons)
    {
      case btnSELECT:
        for (i = 0; i < 3; i++)
        {
          if (read_linky (current_field, i < 2))
            break;
        }
        break;
      case btnUP:
        if (current_field == 0)
        {
          while (fields[++current_field].name) { };
        }
        current_field--;
        break;
      case btnDOWN:
        if (fields[current_field+1].name == NULL)
          current_field = 0;
        else
          current_field++;
        break;
    }

    display_field (&fields[current_field]);
  }
}

/* print integer 'n' in line 'line' starting at 'index' */
/*static void line_print (int line, int n, int index)
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
}*/

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
/*static int get_temp10 ()
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
}*/

/* display temperature on given line */
/*static void show_temp (int line)
{
  // show temperature
  line_print (line, "temp=xx.x C");
  line_print (line, temperature / 10, 5);
  line_print (line, temperature % 10, 8);
  line_show (line);
}*/
 
