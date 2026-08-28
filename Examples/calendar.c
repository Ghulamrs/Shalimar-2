/* calendar.c - the three shapes of switch, in one program.

   c2s lowers a switch three different ways depending on what the arms do,
   and this asks for each of them:

     days()   grouped labels, every arm breaking  -> a plain if / else if chain
     score()  real fall-through                   -> the entry/done pair
     rank()   a break from the middle of an arm   -> a while 1 { ... } wrapper
*/
#include <stdio.h>

/* Grouped labels are NOT fall-through: four labels sharing one arm collapse
   into one condition. Every arm breaks, so this is the plain chain. */
int days(int month, int leap)
{
    int n;

    switch (month) {
    case 4:
    case 6:
    case 9:
    case 11:
        n = 30;
        break;
    case 2:
        n = 28;
        if (leap) {
            n = 29;
        }
        break;
    default:
        n = 31;
        break;
    }
    return n;
}

/* Real fall-through: level 3 earns what levels 2 and 1 earn as well, because
   its arm runs on into theirs. */
int score(int level)
{
    int total;

    total = 0;
    switch (level) {
    case 3:
        total = total + 100;
    case 2:
        total = total + 50;
    case 1:
        total = total + 25;
        break;
    default:
        total = 0;
        break;
    }
    return total;
}

/* A break that leaves the switch from inside an if, in the middle of an arm.
   C's break binds to the switch, not to the if. */
int rank(int points, int bonus)
{
    int band;

    band = 0;
    switch (bonus) {
    case 1:
        band = 1;
        if (points > 90) {
            band = 3;
            break;
        }
        band = band + 1;
        break;
    default:
        band = 0;
        break;
    }
    return band;
}

int main(void)
{
    int m;
    int lv;

    printf("days in each month of a leap year\n");
    for (m = 1; m <= 12; m++) {
        printf("  month %d has %d days\n", m, days(m, 1));
    }

    printf("scores, where a level earns what the ones below it earn\n");
    for (lv = 0; lv <= 3; lv++) {
        printf("  level %d scores %d\n", lv, score(lv));
    }

    printf("bands\n");
    printf("  95 with bonus is band %d\n", rank(95, 1));
    printf("  40 with bonus is band %d\n", rank(40, 1));
    printf("  95 without   is band %d\n", rank(95, 0));
    return 0;
}
