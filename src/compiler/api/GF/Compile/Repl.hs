{-# LANGUAGE LambdaCase #-}

module GF.Compile.Repl (ReplOpts(..), defaultReplOpts, replOptDescrs, getReplOpts, runRepl, runRepl') where

import Control.Monad (unless, forM_, foldM)
import Control.Monad.IO.Class (MonadIO)
import qualified Data.ByteString.Char8 as BS
import Data.Char (isSpace)
import Data.Function ((&))
import Data.Functor ((<&>))
import qualified Data.Map as Map

import System.Console.GetOpt (ArgOrder(RequireOrder), OptDescr(..), ArgDescr(..), getOpt, usageInfo)
import System.Console.Haskeline (InputT, Settings(..), noCompletion, runInputT, getInputLine, outputStrLn)
import System.Directory (getAppUserDataDirectory)

import GF.Compile (batchCompile)
import GF.Compile.Compute.Concrete2 (Globals(Gl), stdPredef, normalFlatForm)
import GF.Compile.Rename (renameSourceTerm)
import GF.Compile.TypeCheck.ConcreteNew (inferLType)
import GF.Data.ErrM (Err(..))
import GF.Grammar.Grammar
  ( Grammar
  , mGrammar
  , Info
  , Module
  , ModuleName
  , ModuleInfo(..)
  , ModuleType(MTResource)
  , ModuleStatus(MSComplete)
  , OpenSpec(OSimple)
  , Location (NoLoc)
  , Term(Typed)
  , prependModule
  )
import GF.Grammar.Lexer (Posn(..), Lang(..), runLangP)
import GF.Grammar.Parser (pTerm)
import GF.Grammar.Printer (TermPrintQual(Unqualified), ppTerm)
import GF.Infra.CheckM (Check, runCheck)
import GF.Infra.Ident (moduleNameS)
import GF.Infra.Option (noOptions)
import GF.Infra.UseIO (justModuleName)
import GF.Text.Pretty (render)
import Debug.Trace

data ReplOpts = ReplOpts
  { lang :: Lang
  , noPrelude :: Bool
  , inputFiles :: [String]
  }

defaultReplOpts :: ReplOpts
defaultReplOpts = ReplOpts GF False []

type Errs a = Either [String] a
type ReplOptsOp = ReplOpts -> Errs ReplOpts

replOptDescrs :: [OptDescr ReplOptsOp]
replOptDescrs =
  [ Option ['h'] ["help"] (NoArg $ \o -> Left [usageInfo "gfci" replOptDescrs]) "Display help."
  , Option [] ["no-prelude"] (flag $ \o -> o { noPrelude = True }) "Don't load the prelude."
  , Option [] ["lang"] (ReqArg (\s o -> case s of
                                          "gf"   -> Right (o { lang = GF })
                                          "bnfc" -> Right (o { lang = BNFC })
                                          "nlg"  -> Right (o { lang = NLG })
                                          _      -> Left ["Unknown language variant: " ++ s])
                               "{gf,bnfc,nlg}")
           "Set the active language variant."
  ]
  where
    flag f = NoArg $ \o -> pure (f o)

getReplOpts :: [String] -> Errs ReplOpts
getReplOpts args = case errs of
  [] -> foldM (&) defaultReplOpts flags <&> \o -> o { inputFiles = inputFiles }
  _  -> Left errs
  where
    (flags, inputFiles, errs) = getOpt RequireOrder replOptDescrs args

execCheck :: MonadIO m => Check a -> (a -> InputT m ()) -> InputT m ()
execCheck c k = case runCheck c of
  Ok (a, warn) -> do
    unless (null warn) $ outputStrLn warn
    k a
  Bad err -> outputStrLn err

replModNameStr :: String
replModNameStr = "<repl>"

replModName :: ModuleName
replModName = moduleNameS replModNameStr

parseThen :: MonadIO m => Lang -> Grammar -> String -> (Term -> InputT m ()) -> InputT m ()
parseThen l g s k = case runLangP l pTerm (BS.pack s) of
  Left (Pn l c, err) -> outputStrLn $ err ++ " (" ++ show l ++ ":" ++ show c ++ ")"
  Right t -> execCheck (renameSourceTerm g replModName t) $ \t -> k t

runRepl' :: Lang -> Globals -> IO ()
runRepl' l gl@(Gl g _) = do
  historyFile <- getAppUserDataDirectory "gfci_history"
  runInputT (Settings noCompletion (Just historyFile) True) repl -- TODO tab completion
  where
    repl = do
      getInputLine "gfci> " >>= \case
        Nothing -> repl
        Just (':' : l) -> let (cmd, arg) = break isSpace l in command cmd (dropWhile isSpace arg)
        Just code -> evalPrintLoop code

    command "t" arg = do
      parseThen l g arg $ \main ->
        execCheck (inferLType gl main) $ \res ->
          forM_ res $ \(t, ty) ->
            let t' = case t of
                       Typed _ _ -> t
                       t         -> Typed t ty
            in outputStrLn $ render (ppTerm Unqualified 0 t')
      outputStrLn "" >> repl
    
    command "q" _ = outputStrLn "Bye!"

    command cmd _ = do
      outputStrLn $ "Unknown REPL command: " ++ cmd
      outputStrLn "" >> repl

    evalPrintLoop code = do -- TODO bindings
      parseThen l g code $ \main ->
        execCheck (inferLType gl main >>= \((t, _):_) -> normalFlatForm gl t) $ \nfs ->
          forM_ (zip [1..] nfs) $ \(i, nf) ->
            outputStrLn $ show i ++ ". " ++ render (ppTerm Unqualified 0 nf)
      outputStrLn "" >> repl

runRepl :: ReplOpts -> IO ()
runRepl (ReplOpts lang noPrelude inputFiles) = do
  -- TODO accept an ngf grammar
  let toLoad = if noPrelude then inputFiles else "prelude/Predef.gfo" : inputFiles
  (g0, opens) <- case toLoad of
    [] -> pure (mGrammar [], [])
    _ -> do
      (_, (_, g0)) <- batchCompile noOptions Nothing toLoad
      pure (g0, OSimple . moduleNameS . justModuleName <$> toLoad)
  let
    modInfo = ModInfo
      { mtype   = MTResource
      , mstatus = MSComplete
      , mflags  = noOptions
      , mextend = []
      , mwith   = Nothing
      , mopens  = opens
      , mexdeps = []
      , msrc    = replModNameStr
      , mseqs   = Nothing
      , jments  = Map.empty
      }
    g = Gl (prependModule g0 (replModName, modInfo)) (if noPrelude then Map.empty else stdPredef g)
  runRepl' lang g
