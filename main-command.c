/* main-command.c
 * Written by Daniel Foote.
 * Started Feb 2005.
 *
 * Command line program to match GPS data and Photo EXIF timestamps
 * together, to figure out where you were at the time.
 * Writes the output back into the GPS exif tags.
 */

/* Copyright 2005 Daniel Foote.
 *
 * This file is part of gpscorrelate.
 *
 * gpscorrelate is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * gpscorrelate is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gpscorrelate; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <getopt.h>
#include <string.h>

#ifndef WIN32
  #include <termios.h>
#endif

#include "gpsstructure.h"
#include "exif-gps.h"
#include "unixtime.h"
#include "gpx-read.h"
#include "correlate.h"

/* Command line options structure. */
static struct option program_options[] = {
	{ "gps", required_argument, 0, 'g' },
	{ "timeadd", required_argument, 0, 'z'},
	{ "no-interpolation", no_argument, 0, 'i'},
	{ "help", no_argument, 0, 'h'},
	{ "verbose", no_argument, 0, 'v'},
	{ "datum", required_argument, 0, 'd'},
	{ "no-write", no_argument, 0, 'n'},
	{ "max-dist", required_argument, 0, 'm'},
	{ "show", no_argument, 0, 's'},
	{ "machine", no_argument, 0, 'o'},
	{ "remove", no_argument, 0, 'r'},
	{ "ignore-tracksegs", no_argument, 0, 't'},
	{ "no-mtime", no_argument, 0, 'M'},
	{ "version", no_argument, 0, 'V'},
	{ "fix-datestamps", no_argument, 0, 'f'},
	{ "degmins", no_argument, 0, 'p'},
	{ "photooffset", required_argument, 0, 'O'},
	{ 0, 0, 0, 0 }
};

/* Function to print the version - near the top for easy modification. */
void PrintVersion(char* ProgramName)
{
	printf("%s, ver. " PACKAGE_VERSION ". Daniel Foote, 2005-2010. GNU GPL.\n", ProgramName);
}

/* Function to print the usage info. */
void PrintUsage(char* ProgramName)
{
	printf("Usage: %s --gps|-g file.gpx [options] file1.jpg file2.jpg ...\n", ProgramName);
	printf("--gps or -g file.gpx: required, specifies GPX file with GPS data.\n");
	printf("--timeadd or -z +/-XX[:XX]: time to add to GPS data to make it match photos.\n");
	printf("   GPS data is in UTC; photos are not likely to be in UTC. Enter the\n");
	printf("   timezone used when taking the photos: eg, +8 for Perth.\n");
	printf("--no-interpolation or -i: disable interpolation between points.\n");
	printf("   Interpolation is linear, points rounded if disabled.\n");
	printf("--verbose or -v: show what has been selected.\n");
	printf("--datum or -d datum: specify measurement datum. If not set, WGS-84 used.\n");
	printf("--no-write or -n: do not write the exif data. Useful with --verbose.\n");
	printf("--max-dist or -m time: max time outside points that photo will be matched.\n");
	printf("   Time is in seconds.\n");
	printf("--show or -s: Just show the GPS data from the given files, if it exists.\n");
	printf("--machine or -o: Just show the GPS data from the given files, machine readable output.\n");
	printf("--remove or -r: Strip GPS tags from the given files, and then quit.\n");
	printf("--ignore-tracksegs or -t: Interpolate between track segments too.\n");
	printf("--no-mtime or -M: Don't change mtime of modified files.\n");
	printf("--fix-datestamps or -f: Fix broken GPS datestamps written with versions < 1.5.2\n");
	printf("--degmins or -p: Write location as DD MM.MM as was default before < 1.5.3.\n");
	printf("--photooffset or -O <seconds>: Offset added to photo time to make it match the GPS.\n");
	printf("--help or -h: display usage/help message.\n");
	printf("--version or -V: display version information.\n");
}

/* Display the information from an existing file. */
void ShowFileDetails(char* File, int MachineReadable)
{
	double Lat, Long, Elev;
	int IncludesGPS = 0;
	char* Time = NULL;
	Lat = Long = Elev = 0;

	Time = ReadExifData(File, &Lat, &Long, &Elev, &IncludesGPS);

	if (Time)
	{
		if (IncludesGPS)
		{
			/* Display the data as CSV if we want
			 * it machine readable. */
			if (MachineReadable)
			{
				printf("\"%s\",\"%s\",%f,%f,%f\n",
					File, Time, Lat, Long, Elev);
			} else {
				printf("%s: %s, Lat %f, Long %f, Elevation %f.\n",
					File, Time, Lat, Long, Elev);
			}
		} else {
			/* Don't display anything if we want machine
			 * readable data and there is no data. */
			if (!MachineReadable)
			{
				printf("%s: %s, No GPS Data.\n",
					File, Time);
			}
		}
	} else {
		/* Say that there was no data, except if we want
		 * machine readable output */
		if (!MachineReadable)
		{
			printf("%s: No EXIF data.\n", File);
		}
	}

	free(Time);
}
			
/* Remove all GPS exif tags from a file. Not really that useful, but... */
/* BUG: presently removes tags even on read-only files. Hmm. */
void RemoveGPSTags(char* File, int NoChangeMtime)
{
	if (RemoveGPSExif(File, NoChangeMtime))
	{
		printf("%s: Removed GPS tags.\n", File);
	} else {
		printf("%s: Tag removal failure.\n", File);
	}
}

/* Fix GPSDatestamp tags, if they were incorrect, as found with versions
 * earlier than 1.5.2. */
void FixDatestamp(char* File, int AdjustmentHours, int AdjustmentMinutes, int NoWriteExif)
{
	/* Read the timestamp data. */
	char DateStamp[12];
	char TimeStamp[12];
	char CombinedTime[24];
	int IncludesGPS = 0;
	char* OriginalDateStamp = NULL;

	OriginalDateStamp = ReadGPSTimestamp(File, DateStamp, TimeStamp, &IncludesGPS);

	if (OriginalDateStamp == NULL)
	{
		printf("%s: No EXIF data.\n", File);
	} else if (IncludesGPS == 0) {
		printf("%s: No GPS data.\n", File);
	} else {
		/* Check the timestamp. */
		time_t PhotoTime = ConvertToUnixTime(OriginalDateStamp, EXIF_DATE_FORMAT,
			AdjustmentHours, AdjustmentMinutes);
		
		strcpy(CombinedTime, DateStamp);
		strcat(CombinedTime, " ");
		strcat(CombinedTime, TimeStamp);

		time_t GPSTime = ConvertToUnixTime(CombinedTime, EXIF_DATE_FORMAT, 0, 0);

		if (PhotoTime != GPSTime) {
			/* Timestamp is wrong. Fix it.
			 * Should be photo time - this also corrects
			 * GPSTimestamp, which was wrong too. */
			if (!NoWriteExif)
			{
				WriteFixedDatestamp(File, PhotoTime);
			}
			char PhotoTimeFormat[100];
			char GPSTimeFormat[100];
			char *tmp = ctime(&PhotoTime);
			strncpy(PhotoTimeFormat, tmp, 100);
			tmp = ctime(&GPSTime);
			strncpy(GPSTimeFormat, tmp, 100);
			printf("%s: Wrong timestamp:\n   Photo:     %s   GPS:       %s   Corrected: %s", 
					File, PhotoTimeFormat, GPSTimeFormat, PhotoTimeFormat);
		} else {
			/* Inside the range. Do nothing! */
			printf("%s: Timestamp is OK: Photo %s (localtime), GPS %s (UTC).\n", File,
					OriginalDateStamp, CombinedTime);
		}
	}

	free(OriginalDateStamp);
}

int main(int argc, char** argv)
{
	/* Say hello. */
	printf("EXIF-GPS Photo matching program.\n");
	printf("Daniel Foote, 2005.\n\n");

	/* If you didn't pass any arguments... */
	if (argc == 1)
	{
		PrintUsage(argv[0]);
		exit(EXIT_SUCCESS);
	}

	/* Parse our command line options. */
	/* But first, some variables to store stuff in. */
	int c;
	
	char* GPSData = NULL;        /* Filename of the file with the GPS data. */
	char* TimeAdjustment = NULL; /* Time adjustment, as passed to program. */
	int TimeZoneHours = 0;       /* Integer version of the timezone. */
	int TimeZoneMins = 0;
	char* Datum = NULL;          /* Datum of input GPS data. */
	int Interpolate = 1;         /* Do we interpolate? By default, yes. */
	int NoWriteExif = 0;         /* Do we not write to file? By default, no. */
	int ShowDetails = 0;         /* Do we show lots of details? By default, no. */
	int FeatherTime = 0;         /* The "feather" time, in seconds. 0 = disabled. */
	int ShowOnlyDetails = 0;
	int MachineReadable = 0;
	int RemoveTags = 0;
	int DoBetweenTrackSegs = 0;
	int NoChangeMtime = 0;
	int FixDatestamps = 0;
	int DegMinSecs = 1;
	int PhotoOffset = 0;

	while (1)
	{
		/* Call getopt to do all the hard work
		 * for us... */
		c = getopt_long(argc, argv, "g:z:ihvd:m:nsortMVfO:",
				program_options, 0);

		if (c == -1) break;

		/* Determine what getopt saw. */
		switch (c)
		{
			case 'g':
				/* This parameter specifies the GPS data.
				 * It must be present. */
				if (optarg)
				{
					GPSData = malloc((sizeof(char) * strlen(optarg)) + 1);
					strncpy(GPSData, optarg, strlen(optarg)+1);
				}
				break;
			case 'z':
				/* This parameter specifies the time to add to the
				 * GPS data to make it match the timezone for
				 * the photos. */
				/* We only store it here, convert it to numbers later. */
				if (optarg)
				{
					TimeAdjustment = malloc((sizeof(char) * strlen(optarg)) + 1);
					strncpy(TimeAdjustment, optarg, strlen(optarg)+1);
				}
				break;
			case 'O':
				PhotoOffset = atoi(optarg);
				break;
			case 'i':
				/* This option disables interpolation. */
				Interpolate = 0;
				break;
			case 'v':
				/* This option asks us to show more info. */
#ifdef WIN32
				printf("--verbose does not work on win32\n");
#else
				ShowDetails = 1;
#endif
				break;
			case 'd':
				/* This option specifies a Datum, if other than WGS-84. */
				if (optarg)
				{
					Datum = malloc((sizeof(char) * strlen(optarg)) + 1);
					strncpy(Datum, optarg, strlen(optarg)+1);
				}
				break;
			case 'n':
				/* This option specifies not to write to file. */
				NoWriteExif = 1;
				break;
			case 'm':
				/* This option gives us the allowable "feather" time. */
				FeatherTime = atoi(optarg);
				break;
			case 'h':
				/* Display the help/usage information. And then quit. */
				PrintUsage(argv[0]);
				exit(EXIT_SUCCESS);
				break;
			case 'V':
				/* Display version information, and then quit. */
				PrintVersion(argv[0]);
				exit(EXIT_SUCCESS);
				break;
			case 'f':
				/* Fix Datestamps. */
				FixDatestamps = 1;
				break;
			case 's':
				/* Show the data in the photos. Mark this. */
				ShowOnlyDetails = 1;
				break;
			case 'o':
				/* Show the data in the photos, machine readable. */
				ShowOnlyDetails = 1;
				MachineReadable = 1;
				break;
			case 'r':
				/* Remove GPS tags from the file. Mark this. */
				RemoveTags = 1;
				break;
			case 't':
				/* Interpolate between track segments. */
				DoBetweenTrackSegs = 1;
				break;
			case 'M':
				NoChangeMtime = 1;
				break;
			case 'p':
				/* Write in old DegMins format. */
				DegMinSecs = 0;
				break;
			case '?':
				/* Unrecognised option. Or, missing argument. */
				/* We should inform the user and let them correct this. */
				printf("Next time, please pass a parameter with that!\n");
				exit(EXIT_FAILURE);
				break;
			default:
				/* Unrecognised code that getopt returned anyway.
				 * Oops... */
				break;
		} /* End switch(c) */
	} /* End While(1) */
	
	/* Check to see if the user passed some files work with. Not much
	 * good if they didn't. */
	if (optind < argc)
	{
		/* You passed some files. Handy! */
	} else {
		/* Hmm. It seems there were no other files... that doesn't work. */
		printf("Nice try! However, next time, pass a few JPEG files to match!\n");
		exit(EXIT_SUCCESS);
	}

	/* If we only wanted to display info on the passed photos, do so now. */
	if (ShowOnlyDetails)
	{
		while (optind < argc)
		{
			ShowFileDetails(argv[optind++], MachineReadable);
		}
		exit(EXIT_SUCCESS);
	}

	/* If we wanted to delete tags, do this now. */
	if (RemoveTags)
	{
		while (optind < argc)
		{
			RemoveGPSTags(argv[optind++], NoChangeMtime);
		}
		exit(EXIT_SUCCESS);
	}

	/* If we wanted to fix datestamps, do this now.
	 * This is to fix incorrect GPSDateStamp values written by versions
	 * earlier than 1.5.2. */
	if (FixDatestamps)
	{
		if (TimeAdjustment == NULL)
		{
			printf("You must give a time adjustment for the photos with -z to fix photos.\n");
			exit(EXIT_SUCCESS);
		}
		
		/* Break up the adjustment and convert to numbers. */
		if (strstr(TimeAdjustment, ":"))
		{
			/* Found colon. Split into two. */
			sscanf(TimeAdjustment, "%d:%d", &TimeZoneHours, &TimeZoneMins);
			if (TimeZoneHours < 0)
			    TimeZoneMins *= -1;
		} else {
			/* No colon. Just parse. */
			TimeZoneHours = atoi(TimeAdjustment);
		}

		while (optind < argc)
		{
			FixDatestamp(argv[optind++], TimeZoneHours, TimeZoneMins, NoWriteExif);
		}
		exit(EXIT_SUCCESS);
	}

	/* Set up any other command line options... */
	if (!Datum)
	{
		Datum = malloc((sizeof(char) * strlen("WGS-84")) + 1);
		strcpy(Datum, "WGS-84");
	}
	if (TimeAdjustment)
	{
		/* Break up the adjustment and convert to numbers. */
		if (strstr(TimeAdjustment, ":"))
		{
			/* Found colon. Split into two. */
			sscanf(TimeAdjustment, "%d:%d", &TimeZoneHours, &TimeZoneMins);
			if (TimeZoneHours < 0)
			    TimeZoneMins *= -1;
		} else {
			/* No colon. Just parse. */
			TimeZoneHours = atoi(TimeAdjustment);
		}

		/* printf("Time zone: %d : %d.\n", TimeZoneHours, TimeZoneMins); */
			
	}

	/* See if the user did pass any GPS data. 
	 * If not, don't continue. */
	if (GPSData == NULL)
	{
		printf("You need to pass some GPS data! Otherwise, nothing to match from!\n");
		exit(EXIT_FAILURE);
	}
	
	/* Read the XML file into memory and extract the "points". */
	/* The returned pointer is the start of a singly-linked list. */
	printf("Reading GPS Data...");
	struct GPSPoint* Points = ReadGPX(GPSData);
	printf("\n");

	if (Points)
	{
		/* GPS Data was read correctly. */
	} else {
		/* GPS Data was not read correctly... */
		/* Tell the user we are bailing.
		 * Not really required, seeing as ReadGPX should
		 * inform the user anyway... but, doesn't hurt! */
		printf("Failure reading/processing GPS data.\n");
		exit(EXIT_FAILURE);
	}

	/* Print a legend for the matching process.
	 * If we're not being verbose. Otherwise, this would be pointless. */
	if (!ShowDetails)
	{
		printf("Legend: . = Ok, / = Interpolated, < = Rounded, - = No match, ^ = Too far.\n");
		printf("        w = Write Fail, ? = No exif date, ! = GPS already present.\n");
	}

	/* Set up our options structure for the correlation function. */
	struct CorrelateOptions Options;
	Options.NoWriteExif   = NoWriteExif;
	Options.NoInterpolate = (Interpolate ? 0 : 1);
	Options.TimeZoneHours = TimeZoneHours;
	Options.TimeZoneMins  = TimeZoneMins;
	Options.FeatherTime   = FeatherTime;
	Options.Datum         = Datum;
	Options.DoBetweenTrkSeg = DoBetweenTrackSegs;
	Options.NoChangeMtime = NoChangeMtime;
	Options.DegMinSecs    = DegMinSecs;
	Options.PhotoOffset   = PhotoOffset;

	Options.MinTime = 0;
	Options.MaxTime = 0;

	Options.Points = Points;

	/* Twig with the terminal settings to make the single character
	 * output stuff work right. */
#ifndef WIN32
	struct termios initial_settings, new_settings;
	if (!ShowDetails)
	{
		tcgetattr(fileno(stdout),&initial_settings);
		new_settings = initial_settings;
		new_settings.c_lflag &= ~ICANON;
		new_settings.c_cc[VMIN] = 1;
		new_settings.c_cc[VTIME] = 0;
		new_settings.c_lflag &= ~ISIG;
		if(tcsetattr(fileno(stdout), TCSANOW, &new_settings) != 0) {
			/* Oops. Oh well. Didn't work. */
			printf("Debug: Broken tty set.\n");
		}
	}
#endif

	/* Make it all look nice and pretty... so the user knows what's going on. */
	printf("\nCorrelate: ");
	if (ShowDetails) printf("\n");
	
	/* A few variables that we'll require later. */
	struct GPSPoint* Result;
	char* File;
	/* Including stats on what happenned. */
	int MatchExact = 0;
	int MatchInter = 0;
	int MatchRound = 0;
	int NotMatched = 0;
	int WriteFail  = 0;
	int TooFar     = 0;
	int NoDate     = 0;
	int GPSPresent = 0;

	/* Now it is time to correlate the photos. Feed one in at a time, and
	 * see what happens.*/
	/* We already checked to make sure that files were passed on the
	 * command line, so just go for it... */
	/* printf("Remaining non-option arguments: %d.\n", argc - optind); */
	while (optind < argc)
	{
		File = argv[optind++];
		/* Pass the file along to Correlate and see what happens. */
		Result = CorrelatePhoto(File, &Options);

		/* Was result NULL? */
		if (Result)
		{
			/* Result not null. But what did happen? */
			if (Options.Result == CORR_OK)
			{
				MatchExact++;
				if (ShowDetails)
				{
					printf("%s: Exact match: ", File);
				} else {
					printf(".");
				}
			}
			if (Options.Result == CORR_INTERPOLATED)
			{
				MatchInter++;
				if (ShowDetails)
				{
					printf("%s: Interpolated: ", File);
				} else {
					printf("/");
				}
			}
			if (Options.Result == CORR_ROUND)
			{
				MatchRound++;
				if (ShowDetails)
				{
					printf("%s: Rounded: ", File);
				} else {
					printf("<");
				}
			}
			if (Options.Result == CORR_EXIFWRITEFAIL)
			{
				WriteFail++;
				if (ShowDetails)
				{
					printf("%s: Exif write failure: ", File);
				} else {
					printf("w");
				}
			}
			if (ShowDetails)
			{
				/* Print out the "point". */
				printf("Lat %f, Long %f, Elev %f.\n",
					Result->Lat, Result->Long,
					Result->Elev);
			}
			/* Ok, that's all from this part... */
		} else {
			/* We got nothing back. One of a few errors. */
			if (Options.Result == CORR_NOMATCH)
			{
				NotMatched++;
				if (ShowDetails)
				{
					printf("%s: No match.\n", File);
				} else {
					printf("-");
				}
			}
			if (Options.Result == CORR_TOOFAR)
			{
				TooFar++;
				if (ShowDetails)
				{
					printf("%s: Too far from nearest point.\n", File);
				} else {
					printf("^");
				}
			}
			if (Options.Result == CORR_NOEXIFINPUT)
			{
				NoDate++;
				if (ShowDetails)
				{
					printf("%s: No date exif tag present.\n", File);
				} else {
					printf("?");
				}
			}
			if (Options.Result == CORR_GPSDATAEXISTS)
			{
				GPSPresent++;
				if (ShowDetails)
				{
					printf("%s: GPS Data already present.\n", File);
				} else {
					printf("!");
				}
			}
			/* Handled all those errors, now... */
		} /* End if Result. */

		/* And, once we've got here, we've finished with that file.
		 * We can now do the next one. Now wasn't that too easy? */
		
	} /* End while parse command line files. */
	
	/* Right, so now we're done. That really wasn't that hard. Right? */

	/* Add a new line if we were doing the not-show-details thing. */
#ifndef WIN32
	if (!ShowDetails)
	{
		printf("\n");
		tcsetattr(fileno(stdout), TCSANOW, &initial_settings);
	}
#endif

	/* Print details of what happenned. */
	printf("\nCompleted correlation process.\n");
	printf("Matched: %5d (%d Exact, %d Interpolated, %d Rounded).\n",
			MatchExact + MatchInter + MatchRound,
			MatchExact, MatchInter, MatchRound);
	printf("Failed:  %5d (%d Not matched, %d Write failure, %d Too Far,\n",
			NotMatched + WriteFail + TooFar + NoDate + GPSPresent,
			NotMatched, WriteFail, TooFar);
	printf("                %d No Date, %d GPS Already Present.)\n",
			NoDate, GPSPresent);


	/* Clean up! */
	FreePointList(Points);
	if (GPSData) free(GPSData);
	if (TimeAdjustment) free(TimeAdjustment);
	if (Datum) free(Datum);
	
	return 0;
}

