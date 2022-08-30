// Copyright (C) 2021  mieru authors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

package log

import (
	"fmt"
	"io"
	"os"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/sirupsen/logrus"
)

// maxClientLogFiles is the maximum number of client log files stored in the disk.
const maxClientLogFiles = 25

// cachedClientLogDir is the directory where client log files are stored.
//
// In Linux, it is typically /home/<user>/.cache/mieru
//
// In Mac OS, it is typically /Users/<user>/Library/Caches/mieru
//
// In Windows, it is typically C:\Users\<user>\AppData\Local\mieru
var cachedClientLogDir string

// init modifies the global logger instance with the desired output file (stdout)
// and customized formatter.
func init() {
	logrus.SetOutput(os.Stdout)
	logrus.SetFormatter(&CliFormatter{})
}

func Fatalf(format string, args ...interface{}) {
	logrus.Fatalf(format, args...)
}

func Errorf(format string, args ...interface{}) {
	logrus.Errorf(format, args...)
}

func Warnf(format string, args ...interface{}) {
	logrus.Warnf(format, args...)
}

func Infof(format string, args ...interface{}) {
	logrus.Infof(format, args...)
}

func Debugf(format string, args ...interface{}) {
	logrus.Debugf(format, args...)
}

func Tracef(format string, args ...interface{}) {
	logrus.Tracef(format, args...)
}

var DebugLevel = logrus.DebugLevel
var TraceLevel = logrus.TraceLevel

type Fields = logrus.Fields

func WithFields(fields Fields) *logrus.Entry {
	return logrus.WithFields(fields)
}

func IsLevelEnabled(level logrus.Level) bool {
	return logrus.IsLevelEnabled(level)
}

func SetLevel(level string) {
	level = strings.ToUpper(level)
	switch level {
	case "FATAL":
		logrus.SetLevel(logrus.FatalLevel)
	case "ERROR":
		logrus.SetLevel(logrus.ErrorLevel)
	case "WARN":
		logrus.SetLevel(logrus.WarnLevel)
	case "INFO":
		logrus.SetLevel(logrus.InfoLevel)
	case "DEBUG":
		logrus.SetLevel(logrus.DebugLevel)
	case "TRACE":
		logrus.SetLevel(logrus.TraceLevel)
	default:
		logrus.Errorf("Unsupported logging level %s", level)
	}
}

func SetOutput(out io.Writer) {
	logrus.SetOutput(out)
}

func SetFormatter(formatter logrus.Formatter) {
	logrus.SetFormatter(formatter)
}

// NewClientLogFile returns a file handler for mieru client to write logs.
func NewClientLogFile() (io.WriteCloser, error) {
	if err := prepareClientLogDir(); err != nil {
		return nil, fmt.Errorf("prepareClientLogDir() failed: %w", err)
	}
	t := time.Now()
	timeStr := fmt.Sprintf("%04d%02d%02d_%02d%02d", t.Year(), t.Month(), t.Day(), t.Hour(), t.Minute())
	pid := strconv.Itoa(os.Getpid())
	fileName := cachedClientLogDir + string(os.PathSeparator) + timeStr + "_" + pid + ".log"
	logFile, err := os.OpenFile(fileName, os.O_WRONLY|os.O_APPEND|os.O_CREATE, 0644)
	if err != nil {
		return nil, fmt.Errorf("failed to create client log file: %v", logFile)
	}
	return logFile, nil
}

// RemoveOldClientLogFiles deletes the oldest client log files.
func RemoveOldClientLogFiles() error {
	if err := prepareClientLogDir(); err != nil {
		return fmt.Errorf("prepareClientLogDir() failed: %w", err)
	}
	entries, err := os.ReadDir(cachedClientLogDir)
	if err != nil {
		return fmt.Errorf("os.ReadDir(%q) failed: %w", cachedClientLogDir, err)
	}
	var logFiles []string
	for _, entry := range entries {
		if !entry.IsDir() && strings.HasSuffix(entry.Name(), ".log") {
			logFiles = append(logFiles, entry.Name())
		}
	}
	if len(logFiles) > maxClientLogFiles {
		toDelete := len(logFiles) - maxClientLogFiles
		sort.Strings(logFiles)
		for i := 0; i < toDelete; i++ {
			if err = os.Remove(cachedClientLogDir + string(os.PathSeparator) + logFiles[i]); err != nil {
				return fmt.Errorf("os.Remove() failed: %w", err)
			}
		}
	}
	return nil
}

// prepareClientLogDir creates client log directory is needed.
// If environment variable MIERU_LOG_DIR is specified, that value is used.
func prepareClientLogDir() error {
	if cachedClientLogDir != "" {
		return os.MkdirAll(cachedClientLogDir, 0755)
	}

	if v, found := os.LookupEnv("MIERU_LOG_DIR"); found {
		v = strings.TrimSpace(v)
		v = strings.TrimSuffix(v, string(os.PathSeparator))
		cachedClientLogDir = v
	} else {
		cacheDir, err := os.UserCacheDir()
		if err != nil {
			return err
		}
		cachedClientLogDir = cacheDir + string(os.PathSeparator) + "mieru"
	}
	return os.MkdirAll(cachedClientLogDir, 0755)
}
